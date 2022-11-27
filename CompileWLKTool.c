// Compile WLK archive tool
// ------------------------
// Valley Bell, written on 2022-11-20

/*
WLK Archive Format v1
------------------
2 bytes - [number of files]-1 (n = value+1) (Little Endian)
0Eh*n bytes - file entry
?? bytes - sound data

File Entry:
1 byte - ?? (usually 00)
1 byte - flags
	Bit 6 (40) - ??
	Bit 7 (80) - 16-bit (clear = 8-bit)
4 bytes - start offset (absolute, Little Endian)
4 bytes - file length (Little Endian)
4 bytes - sample rate (Little Endian)

WLK Archive Format v2
------------------
8 bytes - "WLKF0200"
2 bytes - number of files (n)
2 bytes - flags
	Bit 0 (01) - TOC contains file titles?
	Bit 1 (02) - TOC contains source file paths?
??*n bytes - file entry
	The file entry is variable, based on the main header flags.
	Base size: 16h
	contains file title: add 6
	contains source file path: add 6
	Thus the entries can have from 16h to 22h bytes.
?? bytes - sound data

File Entry:
1 byte - ?? (usually FF)
1 byte - flags
	Bit 0 (01) - ??
	Bit 7 (80) - 16-bit (clear = 8-bit)
4 bytes - start offset (absolute, Little Endian)
4 bytes - file length (Little Endian)
4 bytes - sample rate (Little Endian)
4 bytes - ?? (usually 0)
4 bytes - ?? (usually 0)
[if file title is enabled]
	4 bytes - file title offset (absolute, Little Endian), destination string has no terminator
	2 bytes - file title length (Little Endian)
[if source file path is enabled]
	4 bytes - source file path offset (absolute, Little Endian), destination string has no terminator
	2 bytes - source file path length (Little Endian)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#undef GetFileTitle	// we have our own platform-independent GetFileTitle fuction
#else
#include <limits.h>	// for PATH_MAX
// realpath() is part of stdlib.h
#endif

#ifdef _MSC_VER
#define strdup	_strdup
#endif

#ifdef HAVE_STDINT

#include <stdint.h>
typedef uint8_t	UINT8;
typedef uint16_t	UINT16;
typedef uint32_t	UINT32;

#else	// ! HAVE_STDINT

typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef unsigned int	UINT32;

#endif	// HAVE_STDINT


typedef struct _file_item
{
	char* fileName;
	UINT8 status;
	UINT8 flags;
	UINT32 filePos;
	UINT32 size;
	UINT32 smplRate;
	UINT32 wavDataOfs;
	char* fileTitle;
	char* srcFilePath;
} FILE_ITEM;
typedef struct _file_path_info
{
	UINT32 titleOfs;
	UINT32 titleLen;
	UINT32 pathOfs;
	UINT32 pathLen;
} FILEPATH_INFO;

typedef struct _file_list
{
	size_t alloc;
	size_t count;
	FILE_ITEM* items;
} FILE_LIST;


static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData);
static UINT8 GetWaveInfo(const char* fileName, FILE_ITEM* fi);
static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const void* data);
static UINT8 WriteWaveFile(const char* fileName, const FILE_ITEM* info, const void* data);
static char* GetFullFilePath(const char* relFilePath);
static const char* GetFileTitle(const char* filePath);
static const char* GetFileExtension(const char* filePath);
static int ExtractArchive(const char* arcFileName, const char* outPattern);
static int CreateArchive(const char* arcFileName, const char* fileListName);
static UINT16 ReadLE16(const UINT8* data);
static UINT32 ReadLE32(const UINT8* data);
static void WriteLE16(UINT8* buffer, UINT16 value);
static void WriteLE32(UINT8* buffer, UINT32 value);


#define MODE_NONE		0x00
#define MODE_EXTRACT	0x01
#define MODE_CREATE		0x02

#define ARC_FMT_NONE	0x00
#define ARC_FMT_OLD		0x01
#define ARC_FMT_NEW		0x02


static UINT8 packArcType = ARC_FMT_NEW;
static UINT16 packArcFlags = 0x0003;
static UINT8 useFileTitle = 0;

int main(int argc, char* argv[])
{
	int argbase;
	UINT8 mode;
	
	printf("Compile WLK archive tool\n------------------------\n");
	if (argc < 2)
	{
		printf("Usage: %s [mode/options] archive.mlk out.mid/filelist.txt\n", argv[0]);
		printf("Mode: (required)\n");
		printf("    -x  extract archive, generates out00.wav, out01.wav, etc.\n");
		printf("    -c  create archive, read list of files from filelist.txt\n");
		printf("Options:\n");
		printf("    -n  [extract] name extracted files after original file title\n");
		printf("        Note: Shift-JIS names may fail to save on Western systems\n");
		printf("    -n  [create] save true file name/path to archive\n");
		printf("        default/not set: take file name/path from filelist.txt\n");
		printf("    -f n set archive format version (can be 1/2, default: %u)\n", packArcType);
		printf("    -b n set archive header flags (default: 0x%02X)\n", packArcFlags);
		return 0;
	}
	
	argbase = 1;
	mode = MODE_NONE;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 'x')
		{
			mode = MODE_EXTRACT;
		}
		else if (argv[argbase][1] == 'c')
		{
			mode = MODE_CREATE;
		}
		else if (argv[argbase][1] == 'n')
		{
			useFileTitle = 1;
		}
		else if (argv[argbase][1] == 'f')
		{
			argbase ++;
			if (argbase < argc)
				packArcType = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (argv[argbase][1] == 'b')
		{
			argbase ++;
			if (argbase < argc)
				packArcFlags = (UINT16)strtoul(argv[argbase], NULL, 0);
		}
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 2)
	{
		printf("Insufficient parameters!\n");
		return 0;
	}
	switch(mode)
	{
	case MODE_NONE:
		printf("Please specify a mode!\n");
		return 1;
	case MODE_EXTRACT:
		return ExtractArchive(argv[argbase + 0], argv[argbase + 1]);
	case MODE_CREATE:
		return CreateArchive(argv[argbase + 0], argv[argbase + 1]);
	default:
		printf("Unsupported mode!\n");
		return 1;
	}
	
	return 0;
}

static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData)
{
	FILE* hFile;
	UINT32 readBytes;
	
	hFile = fopen(fileName, "rb");
	if (hFile == NULL)
		return 0xFF;
	
	fseek(hFile, 0, SEEK_END);
	*retSize = ftell(hFile);
	if (*retSize > 0x10000000)
		*retSize = 0x10000000;	// limit to 256 MB
	
	*retData = (UINT8*)realloc(*retData, *retSize);
	fseek(hFile, 0, SEEK_SET);
	readBytes = fread(*retData, 0x01, *retSize, hFile);
	
	fclose(hFile);
	return (readBytes == *retSize) ? 0 : 1;
}

static UINT8 GetWaveInfo(const char* fileName, FILE_ITEM* fi)
{
	FILE* hFile;
	UINT8 chnkHdr[8];
	UINT32 chnkSize;
	UINT8 found;
	UINT8 fmtData[0x10];
	size_t readEl;
	UINT16 formatTag;
	UINT16 nChannels;
	UINT16 bitsPerSmpl;
	
	hFile = fopen(fileName, "rb");
	if (hFile == NULL)
		return 0xFF;	// open error
	
	readEl = fread(chnkHdr, 4, 2, hFile);
	if (readEl < 2 || memcmp(&chnkHdr[0], "RIFF", 4))
	{
		fclose(hFile);
		return 0xF0;	// no RIFF file
	}
	readEl = fread(chnkHdr, 4, 1, hFile);
	if (readEl < 1 || memcmp(&chnkHdr[0], "WAVE", 4))
	{
		fclose(hFile);
		return 0xF1;	// no RIFF WAVE
	}
	
	found = 0x00;
	while(!feof(hFile) && !ferror(hFile))
	{
		size_t fPos;
		readEl = fread(chnkHdr, 4, 2, hFile);
		if (readEl < 2)
			break;
		chnkSize = ReadLE32(&chnkHdr[4]);
		
		fPos = ftell(hFile);
		if (!memcmp(&chnkHdr[0], "data", 4))
		{
			found |= 0x02;
			fi->wavDataOfs = (UINT32)ftell(hFile);
			fi->size = chnkSize;
			break;
		}
		else if (!memcmp(&chnkHdr[0], "fmt ", 4))
		{
			found |= 0x01;
			fread(fmtData, 1, 0x10, hFile);
		}
		fseek(hFile, fPos + chnkSize, SEEK_SET);
	}
	
	fclose(hFile);
	if (!(found & 0x01))
		return 0xF2;	// Format chunk not found.
	if (!(found & 0x02))
 		return 0xF3;	// Data chunk not found.
	
	formatTag = ReadLE16(&fmtData[0x00]);
	nChannels = ReadLE16(&fmtData[0x02]);
	bitsPerSmpl = ReadLE16(&fmtData[0x0E]);
	if (formatTag != 0x0001)	// WAVE_FORMAT_PCM
		return 0x80;	// codec not supported
	if (nChannels != 1)
		return 0x81;	// must be mono
	if (bitsPerSmpl != 8 && bitsPerSmpl != 16)
		return 0x82;	// needs to be 8-bit or 16-bit
	fi->smplRate = ReadLE32(&fmtData[0x04]);
	fi->flags &= ~0x80;
	fi->flags |= (bitsPerSmpl == 16) ? 0x80 : 0x00;
	
	return 0x00;
}

static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const void* data)
{
	FILE* hFile;
	UINT32 writtenBytes;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
		return 0xFF;
	
	writtenBytes = fwrite(data, 1, dataLen, hFile);
	
	fclose(hFile);
	return (writtenBytes == dataLen) ? 0 : 1;
}

static UINT8 WriteWaveFile(const char* fileName, const FILE_ITEM* info, const void* data)
{
	FILE* hFile;
	UINT8 wavHdr[0x2C];
	UINT32 writtenBytes;
	UINT32 fileSize;
	UINT8 channels;
	UINT16 bitDepth;
	UINT16 blockSize;
	UINT32 byteRate;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
		return 0xFF;
	
	channels = 1;
	bitDepth = (info->flags & 0x80) ? 16 : 8;
	blockSize = (channels * bitDepth + 7) / 8;
	byteRate = info->smplRate * blockSize;
	
	memcpy(&wavHdr[0x00], "RIFF", 0x04);
	WriteLE32(&wavHdr[0x04], 0x24 + info->size);	// RIFF chunk size
	memcpy(&wavHdr[0x08], "WAVE", 0x04);
	
	memcpy(&wavHdr[0x0C], "fmt ", 0x04);
	WriteLE32(&wavHdr[0x10], 0x10);				// fmt chunk size
	WriteLE16(&wavHdr[0x14], 0x0001);			// format tag: WAVE_FORMAT_PCM
	WriteLE16(&wavHdr[0x16], channels);			// number of channels
	WriteLE32(&wavHdr[0x18], info->smplRate);	// sample rate
	WriteLE32(&wavHdr[0x1C], byteRate);			// bytes per second
	WriteLE16(&wavHdr[0x20], blockSize);		// block align
	WriteLE16(&wavHdr[0x22], bitDepth);			// bits per sample
	
	memcpy(&wavHdr[0x24], "data", 0x04);
	WriteLE32(&wavHdr[0x28], info->size);	// data chunk size
	
	fileSize = 0x2C + info->size;
	
	writtenBytes  = fwrite(wavHdr, 1, 0x2C, hFile);
	writtenBytes += fwrite(data, 1, info->size, hFile);
	
	fclose(hFile);
	return (writtenBytes == fileSize) ? 0 : 1;
}

static char* GetFullFilePath(const char* relFilePath)
{
#ifdef _WIN32
	char buffer[MAX_PATH];
	DWORD pathLen = GetFullPathName(relFilePath, MAX_PATH, buffer, NULL);
	if (!pathLen)
		return NULL;
	return strdup(buffer);
#else
	return realpath(relFilePath, NULL);
#endif
}

static const char* GetFileTitle(const char* filePath)
{
	const char* sepPos1 = strrchr(filePath, '/');
	const char* sepPos2 = strrchr(filePath, '\\');
	const char* dirSepPos;
	
	if (sepPos1 == NULL)
		dirSepPos = sepPos2;
	else if (sepPos2 == NULL)
		dirSepPos = sepPos1;
	else
		dirSepPos = (sepPos1 < sepPos2) ? sepPos2 : sepPos1;
	return (dirSepPos != NULL) ? &dirSepPos[1] : filePath;
}

static const char* GetFileExtension(const char* filePath)
{
	const char* fileTitle = GetFileTitle(filePath);
	const char* extDotPos = strrchr(fileTitle, '.');
	return extDotPos;
}


static int ExtractArchive(const char* arcFileName, const char* outPattern)
{
	UINT8 arcFormat;
	UINT16 arcHdrFlags;
	UINT16 tocEntryBS;	// base size
	UINT16 tocEntrySize;
	UINT32 arcSize;
	UINT8* arcData;
	UINT8 retVal;
	const char* fileExt;
	char* outName;
	char* outExt;
	FILE* hListFile;
	UINT32 tocPos;
	size_t fileCnt;
	size_t curFile;
	
	fileExt = GetFileExtension(outPattern);
	if (fileExt == NULL)
		fileExt = outPattern + strlen(outPattern);
	outName = (char*)malloc(strlen(outPattern) + 0x10);
	strcpy(outName, outPattern);
	outExt = outName + (fileExt - outPattern);
	
	arcSize = 0;
	arcData = NULL;
	retVal = ReadFileData(arcFileName, &arcSize, &arcData);
	if (retVal)
	{
		if (retVal == 0xFF)
			printf("Error opening %s!\n", arcFileName);
		else
			printf("Unable to fully read %s!\n", arcFileName);
		return 1;
	}
	
	arcFormat = ARC_FMT_NONE;
	if (! memcmp(&arcData[0x00], "WLKF0200", 0x08))
	{
		arcFormat = ARC_FMT_NEW;
	}
	else
	{
		UINT16 fileCnt16 = ReadLE16(&arcData[0x00]) + 1;
		UINT32 file1Pos = ReadLE32(&arcData[0x04]);
		if (file1Pos >= (fileCnt16 * 0x0E) && file1Pos < arcSize)
			arcFormat = ARC_FMT_OLD;
	}
	
	printf("Archive format version: %u\n", arcFormat);
	switch(arcFormat)
	{
	case ARC_FMT_OLD:
		fileCnt = ReadLE16(&arcData[0x00]) + 1;
		arcHdrFlags = 0x00;
		tocEntryBS = 0x0E;
		tocEntrySize = tocEntryBS;
		tocPos = 0x02;
		break;
	case ARC_FMT_NEW:
		fileCnt = ReadLE16(&arcData[0x08]);
		arcHdrFlags = ReadLE16(&arcData[0x0A]);
		printf("Archive flags: 0x%04X\n", arcHdrFlags);
		tocEntryBS = 0x16;
		tocEntrySize = tocEntryBS;
		if (arcHdrFlags & 0x0001)
			tocEntrySize += 0x06;
		if (arcHdrFlags & 0x0002)
			tocEntrySize += 0x06;
		tocPos = 0x0C;
		break;
	default:
		printf("Unable to determine WLK format version!\n");
		return 2;
	}
	
	// extract everything
	strcpy(outExt, ".txt");
	hListFile = fopen(outName, "wt");
	
	if (arcHdrFlags & 0x0003)
		fprintf(hListFile, "#filename\tflags\tsrcPath\tfileTitle\n");
	else
		fprintf(hListFile, "#filename\tflags\n");
	printf("%u %s\n", fileCnt, (fileCnt == 1) ? "file" : "files");
	
	for (curFile = 0; curFile < fileCnt; curFile ++, tocPos += tocEntrySize)
	{
		UINT32 tpOfs;
		FILE_ITEM fi;
		char* outPath;
		
		fi.flags = arcData[tocPos + 0x01];
		fi.filePos = ReadLE32(&arcData[tocPos + 0x02]);
		fi.size = ReadLE32(&arcData[tocPos + 0x06]);
		fi.smplRate = ReadLE32(&arcData[tocPos + 0x0A]);
		fi.fileTitle = NULL;
		fi.srcFilePath = NULL;
		tpOfs = tocEntryBS;
		
		if (arcHdrFlags & 0x0001)
		{
			UINT32 ofs = ReadLE32(&arcData[tocPos + tpOfs + 0x00]);
			UINT32 len = ReadLE16(&arcData[tocPos + tpOfs + 0x04]);
			if (ofs && len)
			{
				fi.fileTitle = (char*)malloc(len + 1);
				memcpy(fi.fileTitle, &arcData[ofs], len);
				fi.fileTitle[len] = '\0';
				tpOfs += 0x06;
			}
		}
		if (arcHdrFlags & 0x0002)
		{
			UINT32 ofs = ReadLE32(&arcData[tocPos + tpOfs + 0x00]);
			UINT32 len = ReadLE16(&arcData[tocPos + tpOfs + 0x04]);
			if (ofs && len)
			{
				fi.srcFilePath = (char*)malloc(len + 1);
				memcpy(fi.srcFilePath, &arcData[ofs], len);
				fi.srcFilePath[len] = '\0';
				tpOfs += 0x06;
			}
		}
		
		// generate file name(ABC.ext -> ABC00.ext)
		sprintf(outExt, "%02u%s", curFile, fileExt);
		printf("File %u/%u: offset: 0x%06X, size 0x%04X\n", 1 + curFile, fileCnt, fi.filePos, fi.size);
		
		if (useFileTitle && fi.fileTitle != NULL)
		{
			const char* titlePtr = GetFileTitle(outName);
			size_t prefixLen = titlePtr - outName;
			size_t totalLen = prefixLen + strlen(fi.fileTitle);
			outPath = (char*)malloc(totalLen + 1);
			strncpy(&outPath[0], outName, titlePtr - outName);
			strcpy(&outPath[prefixLen], fi.fileTitle);
		}
		else
		{
			outPath = outName;
		}
		if (arcHdrFlags & 0x0003)
		{
			fprintf(hListFile, "%s\t0x%02X\t%s\t%s\n", outPath, fi.flags,
				(fi.srcFilePath != NULL) ? fi.srcFilePath : "",
				(fi.fileTitle != NULL) ? fi.fileTitle : "");
		}
		else
		{
			fprintf(hListFile, "%s\t0x%02X\n", outPath, fi.flags);
		}
		retVal = WriteWaveFile(outPath, &fi, &arcData[fi.filePos]);
		if (retVal)
		{
			if (retVal == 0xFF)
				printf("Error writing %s!\n", outPath);
			else
				printf("Error writing %s - file incomplete!\n", outPath);
		}
		if (outPath != outName)
			free(outPath);
		if (fi.fileTitle)
			free(fi.fileTitle);
		if (fi.srcFilePath)
			free(fi.srcFilePath);
	}
	
	fclose(hListFile);
	free(arcData);
	
	printf("Done.\n");
	return 0;
}

static FILE_ITEM* AddFileListItem(FILE_LIST* fl)
{
	if (fl->count >= fl->alloc)
	{
		fl->alloc += 0x10;
		fl->items = (FILE_ITEM*)realloc(fl->items, fl->alloc * sizeof(FILE_ITEM));
	}
	fl->count ++;
	return &fl->items[fl->count - 1];
}

static void FreeFileList(FILE_LIST* fl)
{
	size_t curFile;
	for (curFile = 0; curFile < fl->count; curFile ++)
	{
		free(fl->items[curFile].fileName);
		free(fl->items[curFile].fileTitle);
		free(fl->items[curFile].srcFilePath);
	}
	return;
}

static void RemoveControlChars(char* str)
{
	size_t idx = strlen(str);
	
	while(idx > 0 && (unsigned char)str[idx - 1] < 0x20)
		idx --;
	str[idx] = '\0';
	
	return;
}

static size_t GetColumns(char* line, size_t maxCols, char* colPtrs[], const char* delim)
{
	size_t curCol = 0;
	char* token = line;
	while(token != NULL && curCol < maxCols)
	{
		colPtrs[curCol] = token;
		curCol ++;
		
		token = strpbrk(token, "\t");
		if (token != NULL)
		{
			*token = '\0';
			token ++;
		}
	}
	return curCol;
}

static int CreateArchive(const char* arcFileName, const char* fileListName)
{
	FILE_LIST fileList;
	FILE* hFile;
	UINT32 lineID;
	char lineStr[0x1000];	// 4096 chars should be enough
	UINT16 tocEntryBS;	// base size
	UINT16 tocEntrySize;
	UINT32 tocSize;
	UINT32 payloadEndOfs;
	int result;
	
	hFile = fopen(fileListName, "rt");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", fileListName);
		return 0xFF;
	}
	
	// read file list
	fileList.alloc = 0;
	fileList.count = 0;
	fileList.items = NULL;
	lineID = 0;
	while(! feof(hFile))
	{
		FILE_ITEM* fi;
		char* strPtr;
		char* colPtrs[4];
		size_t colCnt;
		
		strPtr = fgets(lineStr, 0x1000, hFile);
		if (strPtr == NULL)
			break;
		lineID ++;
		RemoveControlChars(lineStr);
		if (strlen(lineStr) == 0 || lineStr[0] == '#')
			continue;
		
		colCnt = GetColumns(lineStr, 4, colPtrs, "\t");
		if (colCnt < 1)
			continue;
		
		fi = AddFileListItem(&fileList);
		fi->fileName = strdup(lineStr);
		if (colCnt >= 2)
			fi->flags = (UINT8)strtoul(colPtrs[1], NULL, 0);
		else
			fi->flags = 0x00;
		fi->srcFilePath = (colCnt >= 3) ? strdup(colPtrs[2]) : NULL;
		fi->fileTitle = (colCnt >= 4) ? strdup(colPtrs[3]) : NULL;
	}
	
	fclose(hFile);
	
	switch(packArcType)
	{
	case ARC_FMT_OLD:
		tocEntryBS = 0x0E;
		tocEntrySize = tocEntryBS;
		tocSize = 0x02 + fileList.count * tocEntrySize;
		break;
	case ARC_FMT_NEW:
		tocEntryBS = 0x16;
		tocEntrySize = tocEntryBS;
		if (packArcFlags & 0x0001)
			tocEntrySize += 0x06;
		if (packArcFlags & 0x0002)
			tocEntrySize += 0x06;
		tocSize = 0x0C + fileList.count * tocEntrySize;
		break;
	default:
		printf("Unable to determine WLK format version!\n");
		FreeFileList(&fileList);
		return 9;
	}
	
	printf("Packing %u %s ...\n", fileList.count, (fileList.count == 1) ? "file" : "files");
	// go through all files, determining file sizes and archive file data offsets
	{
		size_t curFile;
		UINT32 filePos;
		
		filePos = tocSize;
		for (curFile = 0; curFile < fileList.count; curFile ++)
		{
			FILE_ITEM* fi = &fileList.items[curFile];
			fi->status = GetWaveInfo(fi->fileName, fi);
			// Potential error messages will be printed when doing the actual packaging.
			fi->filePos = filePos;
			if (fi->status & 0x80)
				fi->size = 0;
			else
				filePos += fi->size;

			if (useFileTitle && packArcType >= ARC_FMT_NEW)
			{
				free(fi->srcFilePath);
				free(fi->fileTitle);
				fi->srcFilePath = GetFullFilePath(fi->fileName);
				fi->fileTitle = strdup(GetFileTitle(fi->srcFilePath));
			}
		}
		payloadEndOfs = filePos;
	}
	
	hFile = fopen(arcFileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", arcFileName);
		FreeFileList(&fileList);
		return 1;
	}
	
	result = 0;
	{
		size_t curFile;
		UINT32 tocPos;
		UINT32 dataSize;
		UINT8* data;
		FILEPATH_INFO* fpiList;
		UINT32 fnListSize;
		UINT8* fnListData;
		UINT8 retVal;
		
		data = (UINT8*)malloc(tocSize);
		fnListData = NULL;
		
		// generate archive TOC
		printf("Writing TOC ...\n");
		switch(packArcType)
		{
		case ARC_FMT_OLD:
			tocPos = 0x00;
			WriteLE16(&data[tocPos], (UINT16)(fileList.count - 1));	tocPos += 0x02;
			for (curFile = 0; curFile < fileList.count; curFile ++, tocPos += tocEntrySize)
			{
				FILE_ITEM* fi = &fileList.items[curFile];
				data[tocPos + 0x00] = 0x00;
				data[tocPos + 0x01] = fi->flags;
				WriteLE32(&data[tocPos + 0x02], fi->filePos);
				WriteLE32(&data[tocPos + 0x06], fi->size);
				WriteLE32(&data[tocPos + 0x0A], fi->smplRate);
			}
			break;
		case ARC_FMT_NEW:
			fpiList = NULL;
			if (packArcFlags & 0x0003)
			{
				// estimate file title/path lengths and offsets
				fpiList = (FILEPATH_INFO*)calloc(fileList.count, sizeof(FILEPATH_INFO));
				fnListSize = 0x00;
				if (packArcFlags & 0x0001)
				{
					for (curFile = 0; curFile < fileList.count; curFile ++)
					{
						FILE_ITEM* fi = &fileList.items[curFile];
						FILEPATH_INFO* fpi = &fpiList[curFile];
						if (fi->fileTitle != NULL)
						{
							fpi->titleOfs = fnListSize;
							fpi->titleLen = strlen(fi->fileTitle);
							fnListSize += fpi->titleLen;
						}
					}
				}
				if (packArcFlags & 0x0002)
				{
					for (curFile = 0; curFile < fileList.count; curFile ++)
					{
						FILE_ITEM* fi = &fileList.items[curFile];
						FILEPATH_INFO* fpi = &fpiList[curFile];
						if (fi->srcFilePath != NULL)
						{
							fpi->pathOfs = fnListSize;
							fpi->pathLen = strlen(fi->srcFilePath);
							fnListSize += fpi->pathLen;
						}
					}
				}
				fnListData = (UINT8*)malloc(fnListSize);
				
				// generate file lists
				if (packArcFlags & 0x0001)
				{
					for (curFile = 0; curFile < fileList.count; curFile ++)
					{
						FILE_ITEM* fi = &fileList.items[curFile];
						FILEPATH_INFO* fpi = &fpiList[curFile];
						if (fi->fileTitle != NULL)
							memcpy(&fnListData[fpi->titleOfs], fi->fileTitle, fpi->titleLen);
					}
				}
				if (packArcFlags & 0x0002)
				{
					for (curFile = 0; curFile < fileList.count; curFile ++)
					{
						FILE_ITEM* fi = &fileList.items[curFile];
						FILEPATH_INFO* fpi = &fpiList[curFile];
						if (fi->srcFilePath != NULL)
							memcpy(&fnListData[fpi->pathOfs], fi->srcFilePath, fpi->pathLen);
					}
				}
			}
			
			tocPos = 0x00;
			memcpy(&data[tocPos], "WLKF0200", 0x08);	tocPos += 0x08;
			WriteLE16(&data[tocPos], (UINT16)fileList.count);	tocPos += 0x02;
			WriteLE16(&data[tocPos], packArcFlags);	tocPos += 0x02;
			
			for (curFile = 0; curFile < fileList.count; curFile ++, tocPos += tocEntrySize)
			{
				FILE_ITEM* fi = &fileList.items[curFile];
				UINT32 tpOfs;
				data[tocPos + 0x00] = 0xFF;
				data[tocPos + 0x01] = fi->flags;
				WriteLE32(&data[tocPos + 0x02], fi->filePos);
				WriteLE32(&data[tocPos + 0x06], fi->size);
				WriteLE32(&data[tocPos + 0x0A], fi->smplRate);
				WriteLE32(&data[tocPos + 0x0E], 0);
				WriteLE32(&data[tocPos + 0x12], 0);
				tpOfs = tocEntryBS;
				
				if (packArcFlags & 0x0001)
				{
					FILEPATH_INFO* fpi = &fpiList[curFile];
					UINT32 ofs = fpi->titleLen ? (payloadEndOfs + fpi->titleOfs) : 0;
					UINT16 len = (UINT16)fpi->titleLen;
					WriteLE32(&data[tocPos + tpOfs + 0x00], ofs);
					WriteLE16(&data[tocPos + tpOfs + 0x04], len);
					tpOfs += 0x06;
				}
				if (packArcFlags & 0x0002)
				{
					FILEPATH_INFO* fpi = &fpiList[curFile];
					UINT32 ofs = fpi->pathLen ? (payloadEndOfs + fpi->pathOfs) : 0;
					UINT16 len = (UINT16)fpi->pathLen;
					WriteLE32(&data[tocPos + tpOfs + 0x00], ofs);
					WriteLE16(&data[tocPos + tpOfs + 0x04], len);
					tpOfs += 0x06;
				}
			}
			free(fpiList);	fpiList = NULL;
			break;
		}
		fwrite(data, 1, tocSize, hFile);	// write header data
		
		// copy file data into archive
		for (curFile = 0; curFile < fileList.count; curFile ++)
		{
			FILE_ITEM* fi = &fileList.items[curFile];
			printf("Writing data %u/%u (%s) ...\n", 1 + curFile, fileList.count, fi->fileName);
			retVal = ReadFileData(fi->fileName, &dataSize, &data);
			if (retVal)
			{
				printf("Unable to read %s!\n", fi->fileName);
				result = 2;
				continue;
			}
			if (dataSize <= fi->wavDataOfs)
				dataSize = 0;
			else
				dataSize -= fi->wavDataOfs;
			if (dataSize > fi->size)
				dataSize = fi->size;
			
			fseek(hFile, fi->filePos, SEEK_SET);
			fwrite(&data[fi->wavDataOfs], 1, dataSize, hFile);
		}
		
		// copy file name data
		if (fnListData != NULL)
		{
			printf("Writing file names ...\n");
			fwrite(fnListData, 1, fnListSize, hFile);
		}
		
		free(data);
		free(fnListData);
	}
	
	fclose(hFile);
	FreeFileList(&fileList);
	
	printf("Done.\n");
	return result;
}


static UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x00] << 0) | (data[0x01] << 8);
}

static UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x00] <<  0) | (data[0x01] <<  8) |
			(data[0x02] << 16) | (data[0x03] << 24);
}

static void WriteLE16(UINT8* buffer, UINT16 value)
{
	buffer[0x00] = (value >> 0) & 0xFF;
	buffer[0x01] = (value >> 8) & 0xFF;
	return;
}

static void WriteLE32(UINT8* buffer, UINT32 value)
{
	buffer[0x00] = (value >>  0) & 0xFF;
	buffer[0x01] = (value >>  8) & 0xFF;
	buffer[0x02] = (value >> 16) & 0xFF;
	buffer[0x03] = (value >> 24) & 0xFF;
	return;
}
