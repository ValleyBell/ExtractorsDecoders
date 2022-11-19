// Compile MLK archive tool
// ------------------------
// Valley Bell, written on 2022-11-19

/*
MLK Archive Format
------------------
1 byte - number of files (n)
9*n bytes - file entry
?? bytes - MIDI data

File Entry:
1 byte - loop mode (00 = no, 01 = yes)
4 bytes - start offset (absolute, Little Endian)
4 bytes - file length (Little Endian)

MIDI notes:
The "loop start" marker is "CC #31" (usually with value 0 on channel 1). It can be placed on any channel.
It should be noted that the sound engine treats "Meta Event: Key Signature" (FF 59) as loop marker as well.

With songs where multiple loop start markers, the game will use the last one. (see bad loop in Comet Summoner's Boss Theme)
When no loop start marker exists, the song loops from the beginning.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
	UINT8 loopMode;
	UINT32 filePos;
	UINT32 size;
} FILE_ITEM;

typedef struct _file_list
{
	size_t alloc;
	size_t count;
	FILE_ITEM* items;
} FILE_LIST;


static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData);
static size_t GetFileSize(const char* fileName);
static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const void* data);
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


int main(int argc, char* argv[])
{
	int argbase;
	UINT8 mode;
	
	printf("Compile MLK archive tool\n------------------------\n");
	if (argc < 2)
	{
		printf("Usage: %s [mode/options] archive.mlk out.mid/filelist.txt\n", argv[0]);
		printf("Mode: (required)\n");
		printf("    -x  extract archive, generates out00.mid, out01.mid, etc.\n");
		printf("    -c  create archive, read list of files from filelist.txt\n");
		printf("Options:\n");
		printf("    none\n");
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

static size_t GetFileSize(const char* fileName)
{
	FILE* hFile;
	size_t fileSize;
	
	hFile = fopen(fileName, "rb");
	if (hFile == NULL)
		return (size_t)-1;
	
	fseek(hFile, 0, SEEK_END);
	fileSize = ftell(hFile);
	
	fclose(hFile);
	return fileSize;
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
	size_t arcSize;
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
	
	// extract everything
	strcpy(outExt, ".txt");
	hListFile = fopen(outName, "wt");
	
	fileCnt = arcData[0x00];
	fprintf(hListFile, "#filename\tloop\n");
	printf("%u %s\n", fileCnt, (fileCnt == 1) ? "file" : "files");
	
	tocPos = 0x01;
	for (curFile = 0; curFile < fileCnt; curFile ++, tocPos += 0x09)
	{
		UINT8 loopMode = arcData[tocPos + 0x00];
		UINT32 filePos = ReadLE32(&arcData[tocPos + 0x01]);
		UINT32 fileSize = ReadLE32(&arcData[tocPos + 0x05]);
		
		// generate file name(ABC.ext -> ABC00.ext)
		sprintf(outExt, "%02u%s", curFile, fileExt);
		printf("File %u/%u: offset: 0x%06X, size 0x%04X\n", 1 + curFile, fileCnt, filePos, fileSize);
		
		fprintf(hListFile, "%s\t%u\n", outName, loopMode);
		retVal = WriteFileData(outName, fileSize, &arcData[filePos]);
		if (retVal)
		{
			if (retVal == 0xFF)
				printf("Error writing %s!\n", arcFileName);
			else
				printf("Error writing %s - file incomplete!\n", arcFileName);
			continue;
		}
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
		char* colPtrs[2];
		size_t colCnt;
		
		strPtr = fgets(lineStr, 0x1000, hFile);
		if (strPtr == NULL)
			break;
		lineID ++;
		RemoveControlChars(lineStr);
		if (strlen(lineStr) == 0 || lineStr[0] == '#')
			continue;
		
		colCnt = GetColumns(lineStr, 2, colPtrs, "\t");
		if (colCnt < 1)
			continue;
		
		fi = AddFileListItem(&fileList);
		fi->fileName = strdup(lineStr);
		if (colCnt >= 2)
			fi->loopMode = atoi(colPtrs[1]);
		else
			fi->loopMode = 0x00;
	}
	
	fclose(hFile);
	
	printf("Packing %u %s ...\n", fileList.count, (fileList.count == 1) ? "file" : "files");
	// go through all files, determining file sizes and archive file data offsets
	{
		size_t curFile;
		UINT32 filePos;
		
		filePos = 0x01 + fileList.count * 0x09;
		for (curFile = 0; curFile < fileList.count; curFile ++)
		{
			FILE_ITEM* fi = &fileList.items[curFile];
			size_t fileSize = GetFileSize(fi->fileName);
			
			fi->filePos = filePos;
			if (fileSize == (size_t)-1)
				fi->size = 0;	// The error message will be printed later.
			else
				fi->size = (UINT32)fileSize;
			filePos += fi->size;
		}
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
		UINT8 retVal;
		
		dataSize = 0x02 + fileList.count * 0x09;
		data = (UINT8*)malloc(dataSize);
		
		// generate archive TOC
		printf("Writing TOC ...\n");
		tocPos = 0x00;
		data[tocPos] = (UINT8)fileList.count;	tocPos += 0x01;
		for (curFile = 0; curFile < fileList.count; curFile ++, tocPos += 0x09)
		{
			FILE_ITEM* fi = &fileList.items[curFile];
			data[tocPos + 0x00] = fi->loopMode;
			WriteLE32(&data[tocPos + 0x01], fi->filePos);
			WriteLE32(&data[tocPos + 0x05], fi->size);
		}
		fwrite(data, 1, dataSize, hFile);	// write header data
		
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
			
			fseek(hFile, fi->filePos, SEEK_SET);
			fwrite(data, 1, dataSize, hFile);
		}
		free(data);
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
