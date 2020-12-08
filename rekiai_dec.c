// Rekiai Song Unpacker
// --------------------
// Valley Bell, written on 2020-12-08
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdtype.h>


static const char* GetFileExt(const char* filePath);
static void DecompressArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static UINT16 ReadBE16(const UINT8* Data);


int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT32 inLen;
	UINT8* inData;
	
	printf("Rekiai Song Unpacker\n--------------------\n");
	if (argc < 3)
	{
		printf("Usage: rekiai_dec.exe [Options] input.mf output_name\n");
		printf("Supported games: Rekiai (PC-98)\n");
		return 0;
	}
	
	argbase = 1;
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
	if (inLen > 0x100000)
		inLen = 0x100000;	// limit to 1 MB
	
	inData = (UINT8*)malloc(inLen);
	fseek(hFile, 0, SEEK_SET);
	fread(inData, 0x01, inLen, hFile);
	
	fclose(hFile);
	
	DecompressArchive(inLen, inData, argv[argbase + 1]);
	
	free(inData);
	
	return 0;
}

static const char* GetFileExt(const char* filePath)
{
	const char* fileExt;
	const char* dirSep;
	const char* dirSep2;
	
	dirSep = strrchr(filePath, '/');
	dirSep2 = strrchr(filePath, '\\');
	if (dirSep == NULL || (dirSep2 != NULL && dirSep < dirSep2))
		dirSep = dirSep2;
	if (dirSep != NULL)
		filePath = dirSep + 1;
	
	fileExt = strrchr(filePath, '.');
	return (fileExt != NULL) ? fileExt : (filePath + strlen(filePath));
}

static void DecompressArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	static const char* PREFIXES[6] = {".TXT", ".SSG", ".OPN", "_N.MS", "_B2.MS", "_GS.MS"};
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 fileCnt;
	UINT32 curFile;
	UINT32 arcPos;
	UINT32 filePos;
	UINT16 fileLen;
	UINT16 wrtLen;
	FILE* hFile;
	
	fileExt = GetFileExt(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	fileCnt = 6;
	arcPos = 0x04;
	filePos = 0x10;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x02)
	{
		fileLen = ReadBE16(&arcData[arcPos]);
		
		// generate file name (ABC.MF -> _GS.MS)
		strcpy(outExt, PREFIXES[curFile]);
		
		if (! fileLen)
		{
			printf("Skipping %s (no data)\n", outName);
			continue;
		}
		
		wrtLen = fileLen;
		if (curFile == 0 && arcData[filePos + wrtLen - 1] == '\0')
			wrtLen --;	// for song title, omit the trailing \0 character
		
		printf("Writing %s ...\n", outName);
		hFile = fopen(outName, "wb");
		if (hFile == NULL)
		{
			printf("Error writing %s!\n", outName);
			break;
		}
		fwrite(&arcData[filePos], 1, wrtLen, hFile);
		fclose(hFile);
		
		filePos += fileLen;
	}
	printf("Done.\n");
	free(outName);
	
	return;
}

static UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}
