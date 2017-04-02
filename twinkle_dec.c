// Twinkle Soft Decompressor
// -------------------------
// Valley Bell, written on 2017-03-27 / 2017-04-02
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdtype.h>


static UINT8 DetectFileType(UINT32 fileSize, const UINT8* fileData, const char* fileName);
static void DecompressFile(const UINT8* inData, const char* fileName);
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
	
	printf("Twinkle Soft Decompressor\n-------------------------\n");
	if (argc < 3)
	{
		printf("Usage: twinkle_dec.exe [Options] input.bin output.bin\n");
		printf("Options:\n");
		printf("    -0  single/archive autodetection\n");
		printf("    -1  single file (.##1 extention)\n");
		printf("    -2  archive (.##2 extention)\n");
		printf("        Note: File names are generated using the output name.\n");
		printf("        Example: output.bin -> output_00.bin, output_01.bin, etc.\n");
		printf("Supported/verified games: Bunretsu Shugo Shin Twinkle Star\n");
		return 0;
	}
	
	fileFmt = 0;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] >= '0' && argv[argbase][1] <= '2')
		{
			fileFmt = argv[argbase][1] - '0';
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
	
	if (fileFmt == 0)
	{
		fileFmt = DetectFileType(inLen, inData, argv[argbase + 0]);
		printf("Detected format: %u\n", fileFmt);
	}
	switch(fileFmt)
	{
	case 1:
		DecompressFile(inData, argv[argbase + 1]);
		break;
	case 2:
		DecompressArchive(inLen, inData, argv[argbase + 1]);
		break;
	default:
		printf("Unknown format!\n");
		break;
	}
	
	free(inData);
	
	return 0;
}

static UINT8 DetectFileType(UINT32 fileSize, const UINT8* fileData, const char* fileName)
{
	const char* tempPtr;
	UINT16 filePos;
	UINT16 fileType;
	UINT32 dummyData;
	UINT32 comprLen;
	
	if (fileSize < 0x08)
		return 0;
	
	tempPtr = strrchr(fileName, '.');
	if (tempPtr != NULL)
	{
		tempPtr += strlen(tempPtr) - 1;	// go to last character
		if (*tempPtr == '1')	// .xx1 - single compressed file
			return 1;
		else if (*tempPtr == '2')	// .xx2 - archive file
			return 2;
	}
	
	filePos = ReadLE16(&fileData[0x00]);
	fileType = ReadLE16(&fileData[0x02]);
	dummyData = ReadLE32(&fileData[0x04]);
	if (filePos < fileSize && fileType < 0x100 && ! dummyData)
		return 2;	// this should be an archive
	
	comprLen = ReadLE32(&fileData[0x00]);
	if (comprLen <= fileSize - 0x08)
		return 1;	// might be a single compressed file
	
	return 0;	// detection failed
}

static void DecompressFile(const UINT8* inData, const char* fileName)
{
	UINT32 comprSize;
	UINT32 decSize;
	UINT8* decBuffer;
	UINT32 outSize;
	FILE* hFile;
	
	comprSize = ReadLE32(&inData[0x00]);
	decSize = ReadLE32(&inData[0x04]);
	printf("Compressed: %u bytes, decompressed: %u bytes\n", comprSize, decSize);
	decBuffer = (UINT8*)malloc(decSize);
	outSize = LZSS_Decode(comprSize, &inData[0x08], decSize, decBuffer);
	if (outSize != decSize)
		printf("Warning - not all data was decompressed!\n");
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
		return;
	}
	fwrite(decBuffer, 1, decSize, hFile);
	fclose(hFile);
	
	return;
}

static void DecompressArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT16 filePos;
	UINT16 fileType;
	UINT32 fileCnt;
	UINT32 curFile;
	UINT16 arcPos;
	UINT16 minPos;
	
	// detect number of files
	fileCnt = 0;
	minPos = (arcSize <= 0xFFFF) ? arcSize : 0xFFFF;
	for (arcPos = 0x00; arcPos < minPos; arcPos += 0x08, fileCnt ++)
	{
		filePos = ReadLE16(&arcData[arcPos + 0x00]);
		if (filePos < minPos)
			minPos = filePos;
		fileType = ReadLE16(&arcData[arcPos + 0x02]);
		if (fileType >= 0x100)
			break;
		if (ReadLE32(&arcData[arcPos + 0x04]))
			break;
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
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x08)
	{
		filePos = ReadLE16(&arcData[arcPos + 0x00]);
		fileType = ReadLE16(&arcData[arcPos + 0x02]);
		
		// generate file name(ABC.ext -> ABC_00.ext)
		sprintf(outExt, "_%02X%s", curFile, fileExt);
		
		printf("file %u / %u: type: %02X, offset: 0x%04X\n    ", 1 + curFile, fileCnt, fileType, filePos);
		DecompressFile(&arcData[filePos], outName);
	}
	
	return;
}

// LZSS decoder by Haruhiko Okumura, 1989-04-06
// modified to work with memory instead of file streams

#define N		 4096	/* size of ring buffer */
#define F		   18	/* upper limit for match_length */
#define THRESHOLD	2	/* encode string into position and length
						   if match_length is greater than this */

UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
{
	UINT32 inPos, outPos;
	UINT8 text_buf[N];	/* ring buffer of size N,
			with extra F-1 bytes to facilitate string comparison */
	int  i, j, k, r, c;
	unsigned int  flags;
	
	for (i = 0; i < N - F; i++) text_buf[i] = 0x00;
	r = N - F;  flags = 0;
	inPos = outPos = 0;
	while(inPos < inLen && outPos < outLen) {
		if (((flags >>= 1) & 256) == 0) {
			c = inData[inPos++];
			flags = c | 0xff00;		/* uses higher byte cleverly */
		}							/* to count eight */
		if (flags & 1) {
			if (inPos >= inLen) break;
			c = inData[inPos++];
			outData[outPos++] = c;  text_buf[r++] = c;  r &= (N - 1);
		} else {
			if (inPos + 1 >= inLen) break;
			i = inData[inPos++];
			j = inData[inPos++];
			i |= ((j & 0xf0) << 4);  j = (j & 0x0f) + THRESHOLD;
			for (k = 0; k <= j; k++) {
				c = text_buf[(i + k) & (N - 1)];
				if (outPos >= outLen) break;
				outData[outPos++] = c;  text_buf[r++] = c;  r &= (N - 1);
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
