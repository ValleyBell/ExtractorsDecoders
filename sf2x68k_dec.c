// X68000 Street Fighter II Decompressor
// -------------------------------------
// Valley Bell, written on 2017-09-01
// based on Twinkle Soft Decompressor
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdtype.h>


static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
static void DecompressFile(UINT32 inSize, const UINT8* inData, const char* fileName);
static void ExtractArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT32 ReadBE32(const UINT8* Data);


#define COMPRESSION_AUTO	0x00
#define COMPRESSION_YES		0x01
#define COMPRESSION_NO		0x02
static UINT8 cmpDetect;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT32 inLen;
	UINT8* inData;
	
	printf("X68000 Street Fighter II Decompressor\n-------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: sf2x68k_dec.exe [Options] input.bin output.bin\n");
		printf("This will create files output_00.bin, output_01.bin, etc.\n");
		printf("Options:\n");
		printf("    -a  compression autodetection (applies to whole archives) [default]\n");
		printf("    -c  archive contains compressed files\n");
		printf("    -r  archive contains raw files\n");
		return 0;
	}
	
	cmpDetect = COMPRESSION_AUTO;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 'a')
		{
			cmpDetect = COMPRESSION_AUTO;
		}
		else if (argv[argbase][1] == 'c')
		{
			cmpDetect = COMPRESSION_YES;
		}
		else if (argv[argbase][1] == 'r')
		{
			cmpDetect = COMPRESSION_NO;
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
	if (inLen > 0x100000)
		inLen = 0x100000;	// limit to 1 MB
	
	inData = (UINT8*)malloc(inLen);
	fseek(hFile, 0, SEEK_SET);
	fread(inData, 0x01, inLen, hFile);
	
	fclose(hFile);
	
	ExtractArchive(inLen, inData, argv[argbase + 1]);
	
	free(inData);
	
	return 0;
}

static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName)
{
	FILE* hFile;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(data, 0x01, dataLen, hFile);
	fclose(hFile);
	
	return 0x00;
}

static void DecompressFile(UINT32 inSize, const UINT8* inData, const char* fileName)
{
	UINT32 decSize;
	UINT8* decBuffer;
	UINT32 outSize;
	
	decSize = 0x100000;	// 1 MB should be more than enough
	//printf("Compressed: %u bytes, decompressed: %u bytes\n", inSize, decSize);
	decBuffer = (UINT8*)malloc(decSize);
	outSize = LZSS_Decode(inSize, inData, decSize, decBuffer);
	if (outSize >= decSize)
		printf("Warning - not all data was decompressed!\n");
	
	WriteFileData(outSize, decBuffer, fileName);
	
	free(decBuffer);	decBuffer = NULL;
	
	return;
}

static void ExtractArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
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
	UINT8 isCompr;
	
	// detect number of files
	fileCnt = 0;
	isCompr = 0x01;
	minPos = arcSize;
	for (arcPos = 0x00; arcPos < minPos; arcPos += 0x08, fileCnt ++)
	{
		filePos = ReadBE32(&arcData[arcPos + 0x00]);
		if (filePos < minPos)
			minPos = filePos;
		fileSize = ReadBE32(&arcData[arcPos + 0x04]);
		if (filePos + fileSize > arcSize)
			break;
		// Assume that every compressed file starts with an FF byte.
		// (might fail if there are lots of repeated bytes at the beginning)
		if (filePos < arcSize && arcData[filePos] != 0xFF)
			isCompr = 0x00;
	}
	//printf("Detected %u %s.\n", fileCnt, (fileCnt == 1) ? "file" : "files");
	if (cmpDetect == COMPRESSION_AUTO)
		printf("Compression detected: %s\n", (isCompr ? "Yes" : "No"));
	else if (cmpDetect == COMPRESSION_NO)
		isCompr = 0x00;
	else if (cmpDetect == COMPRESSION_YES)
		isCompr = 0x01;
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = 0x00;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x08)
	{
		filePos = ReadBE32(&arcData[arcPos + 0x00]);
		fileSize = ReadBE32(&arcData[arcPos + 0x04]);
		
		// generate file name(ABC.ext -> ABC_00.ext)
		sprintf(outExt, "_%02X%s", curFile, fileExt);
		
		printf("file %u / %u\n    ", 1 + curFile, fileCnt);
		if (isCompr)
			DecompressFile(fileSize, &arcData[filePos], outName);
		else
			WriteFileData(fileSize, &arcData[filePos], outName);
	}
	
	return;
}

// original LZSS decoder by Haruhiko Okumura, 1989-04-06
// This is a modified version that doesn't use a ring buffer.
// Instead output data is referenced directly.

#define THRESHOLD	2	/* encode string into position and length
						   if match_length is greater than this */

UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
{
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
			j = inData[inPos++];
			i = inData[inPos++];
			i |= ((j & 0xf0) << 4);  j = (j & 0x0f) + THRESHOLD;
			if (i > outPos)
			{
				printf("Decompression Error at 0x%06X: Accessing out-of-bounds data!\n", inPos - 2);
				break;
			}
			for (k = 0; k <= j; k++) {
				if (outPos >= outLen) break;
				outData[outPos++] = outData[outPos - i];
			}
		}
	}
	return outPos;
}

static UINT32 ReadBE32(const UINT8* Data)
{
	return	(Data[0x00] << 24) | (Data[0x01] << 16) |
			(Data[0x02] <<  8) | (Data[0x03] <<  0);
}
