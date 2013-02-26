// LBX Unpacker for Princess Maker 2
// ------------
// Written by Valley Bell, 26 February 2013

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <direct.h>	// for mkdir


// Type Definitions for short types
typedef unsigned char	UINT8;
typedef signed char 	INT8;

typedef unsigned short	UINT16;
typedef signed short	INT16;

typedef unsigned int	UINT32;
typedef signed int		INT32;


#define printerr(x)		fprintf(stderr, x)
#define printerr2(x, y)	fprintf(stderr, x, y)


int main(int argc, char* argv[]);
UINT8 UnpackLBXArchive(const char* InputFile, const char* ExtractPath);
static void CreatePath(const char* FileName);
static char* strchr_dir(const char* String);
static void RTrimSpaces(char* String);
static void PrintPMDIBMTags(const UINT32 FileSize, const UINT8* FileData);


typedef struct lbx_toc
{
	char Name[0x0C];
	UINT32 Position;
	UINT32 Size;
} LBX_TOC;


int main(int argc, char* argv[])
{
	UINT8 RetVal;
	
	printf("LBX Unpacker Unpacker\n---------------------\n");
	
	if (argc < 3)
	{
		printf("Usage: archive.lbx destpath/\n");
		return 1;
	}
	
	RetVal = UnpackLBXArchive(argv[1], argv[2]);
	getchar();
	
	return RetVal >> 3;
}

UINT8 UnpackLBXArchive(const char* InputFile, const char* ExtractPath)
{
	UINT16 FileCount;
	UINT32 TOCPos;
	LBX_TOC* Files;
	UINT32 CurFile;
	FILE* hFile;
	FILE* hFileOut;
	UINT32 TempLng;
	LBX_TOC* TempFile;
	UINT8* FileBuf;
	char* FileName;
	char* FileNameTitle;
	
	hFile = fopen(InputFile, "rb");
	if (hFile == NULL)
	{
		printerr("Error opening file!\n");
		return 0x10;
	}
	
	fseek(hFile, -0x06, SEEK_END);
	
	TempLng = ftell(hFile);		// get TOC end offset
	fread(&FileCount, 0x02, 0x01, hFile);
	fread(&TOCPos, 0x04, 0x01, hFile);
	
	if (TOCPos + FileCount * 0x14 > TempLng)
	{
		fclose(hFile);
		printerr("TOC too large! File invalid!\n");
		return 0x20;
	}
	
	printf("LBX contains %u files.\n", FileCount);
	
	Files = (LBX_TOC*)malloc(FileCount * sizeof(LBX_TOC));
	printf("Reading TOC ...");
	fseek(hFile, TOCPos, SEEK_SET);
	TempLng = fread(Files, 0x14, FileCount, hFile);
	if (TempLng < FileCount)
	{
		printerr2("Warning: Could read only %u TOC entries!", TempLng);
		FileCount = TempLng;
	}
	printf("  OK\n");
	
	printf("Extracting Files ...\n");
	FileName = (char*)malloc(strlen(ExtractPath) + 0x10);
	strcpy(FileName, ExtractPath);
	FileNameTitle = FileName + strlen(FileName);
	
	for (CurFile = 0x00; CurFile < FileCount; CurFile ++)
	{
		TempFile = &Files[CurFile];
		
		strncpy(FileNameTitle, TempFile->Name, 0x0C);
		FileNameTitle[0x0C] = '\0';
		RTrimSpaces(FileNameTitle);
		
		CreatePath(FileName);
		hFileOut = fopen(FileName, "wb");
		if (hFileOut == NULL)
		{
			printf("Error: Can't open %s!\n", FileNameTitle);
		}
		else
		{
			printf("%.12s\n", TempFile->Name);
			FileBuf = (UINT8*)malloc(TempFile->Size);
			
			fseek(hFile, TempFile->Position, SEEK_SET);
			fread(FileBuf, 0x01, TempFile->Size, hFile);
			fwrite(FileBuf, 0x01, TempFile->Size, hFileOut);
			
			if (FileBuf[0] == 0x02 && FileBuf[1] == 0x1A && FileBuf[2] == 0x00)
				PrintPMDIBMTags(TempFile->Size, FileBuf);
			
			free(FileBuf);		FileBuf = NULL;
			fclose(hFileOut);	hFileOut = NULL;
		}
	}
	printf("Done.\n");
	free(FileName);
	
	return 0x00;
}

static void CreatePath(const char* FileName)
{
	char* Path;
	char ChrBak;
	char* TempStr;
	
	Path = (char*)malloc(strlen(FileName) + 0x01);
	strcpy(Path, FileName);
	TempStr = strchr_dir(Path);
	while(TempStr != NULL)
	{
		ChrBak = *TempStr;
		*TempStr = 0x00;
		mkdir(Path);
		*TempStr = ChrBak;
		TempStr ++;
		
		TempStr = strchr_dir(TempStr);
	}
	
	return;
}

static char* strchr_dir(const char* String)
{
	while(*String != '\0')
	{
		if (*String == '/' || *String == '\\')
			return (char*)String;
		String ++;
	}
	
	return NULL;
}


static void RTrimSpaces(char* String)
{
	char* CurChr;
	
	CurChr = String;
	while(*CurChr != '\0')
		CurChr ++;
	CurChr --;
	
	while(*CurChr == ' ' && CurChr >= String)
	{
		*CurChr = '\0';
		CurChr --;
	}
	
	return;
}

static void PrintPMDIBMTags(const UINT32 FileSize, const UINT8* FileData)
{
	UINT16 TagOffsets[6];
	const char* DataPtr;
	UINT8 CurTag;
	
	memcpy(TagOffsets, &FileData[FileSize - 0x0E], 0x0C);
	DataPtr = (const char*)FileData + 1;
	
	for (CurTag = 0; CurTag < 6; CurTag ++)
	{
		if (TagOffsets[CurTag] >= FileSize)
			continue;
		printf("\tTag %u:\t%s\n", CurTag, DataPtr + TagOffsets[CurTag]);
	}
	
	return;
}
