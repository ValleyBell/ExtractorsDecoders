// Genocide Square Decompressor
// ----------------------------
// Valley Bell, written on 2018-05-25
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stdtype.h"


static void DecompressFile(UINT32 inLen, const UINT8* inData, const char* fileName);
static void DecompressArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT16 ReadLE16(const UINT8* Data);
static UINT32 ReadLE32(const UINT8* Data);


int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT32 inLen;
	UINT8* inData;
	UINT8 fileFmt;
	
	printf("Genocide Square Decompressor\n----------------------------\n");
	if (argc < 3)
	{
		printf("Usage: gensqu_dec.exe [Options] archive.ard output.bin\n");
		printf("Options:\n");
		printf("    -f  single file\n");
		printf("    -a  archive (.ard, default)\n");
		printf("        Note: File names are generated using the output name.\n");
		printf("        Example: output.bin -> output_00.bin, output_01.bin, etc.\n");
		printf("Supported/verified games: Bunretsu Shugo Shin Twinkle Star\n");
		return 0;
	}
	
	fileFmt = 0;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 'a')
			fileFmt = 0;
		else if (argv[argbase][1] == 'f')
			fileFmt = 1;
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
	
	switch(fileFmt)
	{
	case 0:
		DecompressArchive(inLen, inData, argv[argbase + 1]);
		break;
	case 1:
		DecompressFile(inLen, inData, argv[argbase + 1]);
		break;
	default:
		printf("Unknown format!\n");
		break;
	}
	
	free(inData);
	
	return 0;
}

static void DecompressFile(UINT32 inLen, const UINT8* inData, const char* fileName)
{
	UINT32 decSize;
	UINT8* decBuffer;
	UINT32 outSize;
	FILE* hFile;
	
	decSize = ReadLE32(&inData[0x00]);
	printf("Compressed: %u bytes, decompressed: %u bytes\n", inLen, decSize);
	decBuffer = (UINT8*)malloc(decSize);
	outSize = LZSS_Decode(inLen, &inData[0x04], decSize, decBuffer);
	if (outSize != decSize)
		printf("Warning - not all data was decompressed!\n");
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		free(decBuffer);
		printf("Error writing %s!\n", fileName);
		return;
	}
	fwrite(decBuffer, 1, decSize, hFile);
	fclose(hFile);
	free(decBuffer);
	
	return;
}

static void DecompressArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 filePos;
	UINT32 fileSize;
	UINT32 fileCnt;
	UINT32 curFile;
	UINT32 arcPos;
	UINT32 minPos;
	
	// detect number of files
	fileCnt = 0;
	minPos = arcSize;
	for (arcPos = 0x00; arcPos < minPos; arcPos += 0x04, fileCnt ++)
	{
		filePos = ReadLE32(&arcData[arcPos + 0x00]);
		if (! filePos)
			break;	// the End-Of-TOC marker seems to be a file offset of 0
		if (filePos < minPos)
			minPos = filePos;
	}
	//printf("Detected %u %s.\n", fileCnt, (fileCnt == 1) ? "file" : "files");
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = 0x00;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x04)
	{
		filePos = ReadLE32(&arcData[arcPos]);
		fileSize = ReadLE32(&arcData[arcPos + 0x04]);
		if (! fileSize)
			fileSize = arcSize;
		fileSize -= filePos;
		
		// generate file name(ABC.ext -> ABC_00.ext)
		sprintf(outExt, "_%02X%s", curFile, fileExt);
		
		printf("file %u / %u: offset: 0x%06X\n    ", 1 + curFile, fileCnt, filePos);
		DecompressFile(fileSize, &arcData[filePos], outName);
	}
	
	return;
}

// custom LZSS variant used in Genocide Square (FM-Towns)
// The decompression routine is stored at RAM offset 00600B5C.
UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
{
	// routine is loaded to offset 00600B5C
	UINT32 inPos, outPos;
	unsigned int i, j, k;
	unsigned int flags, fbits;
	
	flags = 0;  fbits = 1;
	inPos = outPos = 0;
	while(inPos < inLen && outPos < outLen) {
		flags <<= 1;  fbits --;
		if (!fbits) {
			flags = inData[inPos++];
			fbits = 8;
		}
		if (flags & 0x80) {
			if (inPos >= inLen) break;
			outData[outPos++] = inData[inPos++];
		} else {
			if (inPos + 1 >= inLen) break;
			// 00600BBF
			flags <<= 1;  fbits --;
			if (!fbits) {
				flags = inData[inPos++];
				fbits = 8;
			}
			if (flags & 0x80)
			{
				// 00600BF2 - duplicate last byte
				j = inData[inPos++];
				if (j == 0)
					break;	// data end
				for (k = 0; k <= j; k++) {
					if (outPos >= outLen) break;
					outData[outPos++] = outData[outPos - 1];
				}
			}
			else
			{
				// 00600BC3 - copy previous section
				flags <<= 1;  fbits --;
				if (!fbits) {
					flags = inData[inPos++];
					fbits = 8;
				}
				j = (flags & 0x80) >> 6;
				flags <<= 1;  fbits --;
				if (!fbits) {
					flags = inData[inPos++];
					fbits = 8;
				}
				j |= (flags & 0x80) >> 7;
				if (j > 0)
				{
					// 00600BD2
					i = 0x100 - inData[inPos++];
				}
				else
				{
					// 00600BE2
					i = ReadLE16(&inData[inPos]);	inPos += 2;
					j = i & 0x0f;
					i = 0x1000 - (i >> 4);
				}
				for (k = 0; k <= j; k++) {
					if (outPos >= outLen) break;
					outData[outPos++] = outData[outPos - i];
				}
			}
		}
	}
	return outPos;
}

static UINT16 ReadLE16(const UINT8* Data)
{
	return	(Data[0x00] <<  0) | (Data[0x01] <<  8);
}

static UINT32 ReadLE32(const UINT8* Data)
{
	return	(Data[0x00] <<  0) | (Data[0x01] <<  8) |
			(Data[0x02] << 16) | (Data[0x03] << 24);
}
