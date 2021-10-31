// Fox Ranger Music Extractor
// --------------------------
// Valley Bell, written on 2021-10-31
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STDINT

#include <stdint.h>
typedef uint8_t	UINT8;
typedef uint16_t	UINT16;

#else	// ! HAVE_STDINT

typedef unsigned char	UINT8;
typedef unsigned short	UINT16;

#endif	// HAVE_STDINT


static void ExtractArchive(size_t arcSize, const UINT8* arcData, size_t fileCnt, const char* fileName);
static void DecryptData(size_t dataLen, UINT8* dst, const UINT8* src);
static const char* GetFileTitle(const char* filePath);
static const char* GetFileExtension(const char* filePath);
static UINT16 ReadLE16(const UINT8* data);


static UINT8 decodeKey = 0x6B;
static size_t songCnt = 20;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	size_t inLen;
	UINT8* inData;
	
	printf("Fox Ranger Music Extractor\n--------------------------\n");
	if (argc < 2)
	{
		printf("Usage: %s [options] archive.dat out.mid\n");
		printf("This will generate out00.mid, out01.mid, etc.\n");
		printf("\n");
		printf("Options:\n");
		printf("    -k  specify XOR decode key (default: 0x%02X)\n", decodeKey);
		printf("    -n  set number of songs in the file (default: %u)\n", (unsigned int)songCnt);
		return 0;
	}
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 'k')
		{
			argbase ++;
			if (argbase < argc)
				decodeKey = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (argv[argbase][1] == 'n')
		{
			argbase ++;
			if (argbase < argc)
				songCnt = (size_t)strtoul(argv[argbase], NULL, 0);
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
	
	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
		return 1;
	
	fseek(hFile, 0, SEEK_END);
	inLen = ftell(hFile);
	if (inLen > 0x1000000)
		inLen = 0x1000000;	// limit to 16 MB
	
	inData = (UINT8*)malloc(inLen);
	fseek(hFile, 0, SEEK_SET);
	fread(inData, 0x01, inLen, hFile);
	
	fclose(hFile);
	
	ExtractArchive(inLen, inData, songCnt, argv[argbase + 1]);
	
	free(inData);
	
	return 0;
}

static void ExtractArchive(size_t arcSize, const UINT8* arcData, size_t fileCnt, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	FILE* hFile;
	size_t tocPos;
	size_t filePos;
	size_t fileSize;
	size_t curFile;
	UINT8* decBuf;
	
	fileExt = GetFileExtension(fileName);
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	filePos = fileCnt * 0x02;
	tocPos = 0x00;
	decBuf = NULL;
	for (curFile = 0; curFile < fileCnt; curFile ++, tocPos += 0x02)
	{
		fileSize = ReadLE16(&arcData[tocPos]);
		
		// generate file name(ABC.ext -> ABC00.ext)
		if (fileCnt > 1)
			sprintf(outExt, "%02u%s", curFile, fileExt);
		
		printf("File %u / %u: offset: 0x%06X, size 0x%04X\n", 1 + curFile, fileCnt, filePos, fileSize);
		decBuf = (UINT8*)realloc(decBuf, fileSize);
		if (decBuf == NULL)
		{
			printf("Memory allocation failed!\n");
			break;
		}
		DecryptData(fileSize, decBuf, &arcData[filePos]);
		filePos += fileSize;
		
		hFile = fopen(outName, "wb");
		if (hFile == NULL)
		{
			printf("Error writing %s!\n", outName);
			continue;
		}
		fwrite(decBuf, 1, fileSize, hFile);
		fclose(hFile);
	}
	free(decBuf);
	
	return;
}

static void DecryptData(size_t dataLen, UINT8* dst, const UINT8* src)
{
	size_t curPos;
	for (curPos = 0x00; curPos < dataLen; curPos ++)
		dst[curPos] = src[curPos] ^ decodeKey;
	return;
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

static UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x00] << 0) | (data[0x01] << 8);
}
