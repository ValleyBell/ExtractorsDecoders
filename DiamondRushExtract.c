#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char	UINT8;
typedef unsigned int	UINT32;

typedef struct _file_toc
{
	UINT32 Offset;
	UINT32 Length;
} FILE_TOC;


#define FCC_MTRK	0x6468544D
#define FCC_PNG		0x474E5089

#define BUFFER_SIZE	0x100
UINT8 FileCount;
FILE_TOC* Files;
UINT32 HdrOffset;

int main(int argc, char* argv[])
{
	FILE* hFileIn;
	FILE* hFileOut;
	UINT8 CurFile;
	UINT8 FileNumChrs;
	char* FileBase;
	char* OutName;
	const char* FileExt;
	char Buffer[BUFFER_SIZE];
	UINT32 RemBytes;
	UINT32 WrtBytes;
	
	if (argc < 2)
	{
		printf("Usage: DRExtract.exe snd.f\n");
		return 0;
	}
	
	hFileIn = fopen(argv[1], "rb");
	if (hFileIn == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	FileBase = (char*)malloc(strlen(argv[1]) + 1);
	strcpy(FileBase, argv[1]);
	OutName = strrchr(FileBase, '.');
	if (OutName != NULL)
		*OutName = '\0';
	
	OutName = (char*)malloc(strlen(FileBase) + 0x10);
	
	FileCount = (UINT8)fgetc(hFileIn);
	printf("%hu files found.\n", FileCount);
	if (FileCount <= 10)
		FileNumChrs = 1;
	else
		FileNumChrs = 2;
	
	Files = (FILE_TOC*)malloc(sizeof(FILE_TOC) * FileCount);
	fread(Files, sizeof(FILE_TOC), FileCount, hFileIn);
	
	HdrOffset = ftell(hFileIn);
	printf("Header Offset: 0x%04u\n", HdrOffset);
	
	for (CurFile = 0x00; CurFile < FileCount; CurFile ++)
	{
		fseek(hFileIn, HdrOffset + Files[CurFile].Offset, SEEK_SET);
		RemBytes = Files[CurFile].Length;
		
		fread(&WrtBytes, 0x04, 0x01, hFileIn);	// read first 4 bytes
		fseek(hFileIn, -4, SEEK_CUR);
		
		switch(WrtBytes)	// select file extention based on file header
		{
		case FCC_MTRK:
			FileExt = "mid";
			break;
		case FCC_PNG:
			FileExt = "png";
			break;
		default:
			FileExt = "bin";
			break;
		}
		sprintf(OutName, "%s_%0*hu.%s", FileBase, FileNumChrs, CurFile, FileExt);
		printf("Extracting %s (%u bytes) ...", OutName, RemBytes);
		
		hFileOut = fopen(OutName, "wb");
		if (hFileOut == NULL)
		{
			printf("Error opening file %s!\n", OutName);
			continue;
		}
		
		while(RemBytes)
		{
			WrtBytes = (RemBytes <= BUFFER_SIZE) ? RemBytes : BUFFER_SIZE;
			WrtBytes = fread(Buffer, 0x01, WrtBytes, hFileIn);
			if (! WrtBytes)
			{
				printf("Read error!\n");
				break;
			}
			
			fwrite(Buffer, 0x01, WrtBytes, hFileOut);
			
			RemBytes -= WrtBytes;
		}
		
		fclose(hFileOut);
		
		printf("\n");
	}
	
	fclose(hFileIn);
	printf("Done.\n");
	
	free(FileBase);
	free(OutName);
	
	return 0;
}
