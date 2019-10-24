// Wolfteam Decompressor
// ---------------------
// Valley Bell, written on 2019-10-23
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdtype.h>


// Byte Order constants
#define BO_LE	0x01	// Little Endian
#define BO_BE	0x02	// Big Endian

static void DecompressFile(const UINT8* inData, const char* fileName);
static void DecompressMultiFile(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT16 ReadLE16(const UINT8* data);
static UINT16 ReadBE16(const UINT8* data);
static UINT16 ReadUInt16(const UINT8* data);
static UINT32 ReadLE32(const UINT8* data);
static UINT32 ReadBE32(const UINT8* data);
static UINT32 ReadUInt32(const UINT8* data);

static UINT8 fmtByteOrder = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT32 inLen;
	UINT8* inData;
	UINT8 fileFmt;
	
	printf("Wolfteam Decompressor\n---------------------\n");
	if (argc < 3)
	{
		printf("Usage: %s [Options] input.bin output.bin\n", argv[0]);
		printf("Options:\n");
		printf("    -1  multiple concatenated compressed files (default)\n");
		printf("    -2  uncompressed archive with TOC\n");
		printf("    -l  Byte Order: Little Endian\n");
		printf("    -b  Byte Order: Big Endian\n");
		printf("        Note: File names are generated using the output name.\n");
		printf("        Example: output.bin -> output_00.bin, output_01.bin, etc.\n");
		return 0;
	}
	
	fileFmt = 0;
	fmtByteOrder = 0;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 'l')
		{
			fileFmt = 1;
		}
		else if (argv[argbase][1] == '2')
		{
			fileFmt = 2;
		}
		else if (argv[argbase][1] == 'l')
		{
			fmtByteOrder = BO_LE;
		}
		else if (argv[argbase][1] == 'b')
		{
			fmtByteOrder = BO_BE;
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
	
	if (fileFmt == 0)
	{
		fileFmt = 1;
		//printf("Detected format: %u\n", fileFmt);
	}
	switch(fileFmt)
	{
	case 1:
		if (fmtByteOrder == 0)
		{
			// read data length of first file
			UINT32 valLE = ReadLE32(&inData[0x00]);
			UINT32 valBE = ReadBE32(&inData[0x00]);
			// the value with the correct order is smaller
			fmtByteOrder = (valLE < valBE) ? BO_LE : BO_BE;
			printf("Detected byte order: %s Endian\n", (fileFmt == BO_LE) ? "Little" : "Big");
		}
		DecompressMultiFile(inLen, inData, argv[argbase + 1]);
		break;
	case 2:
		if (fmtByteOrder == 0)
		{
			// read number of files
			UINT16 valLE = ReadLE16(&inData[0x00]);
			UINT16 valBE = ReadBE16(&inData[0x00]);
			// the value with the correct order is smaller
			fmtByteOrder = (valLE < valBE) ? BO_LE : BO_BE;
			printf("Detected byte order: %s Endian\n", (fileFmt == BO_LE) ? "Little" : "Big");
		}
		ExtractArchive(inLen, inData, argv[argbase + 1]);
		break;
	default:
		printf("Unknown format!\n");
		break;
	}
	
	free(inData);
	
	return 0;
}

static void DecompressFile(const UINT8* inData, const char* fileName)
{
	UINT32 comprSize;
	UINT32 decSize;
	UINT8* decBuffer;
	UINT32 outSize;
	FILE* hFile;
	
	comprSize = ReadUInt32(&inData[0x00]);
	decSize = ReadUInt32(&inData[0x04]);
	printf("Compressed: %u bytes, decompressed: %u bytes\n", comprSize, decSize);
	decBuffer = (UINT8*)malloc(decSize);
	outSize = LZSS_Decode(comprSize, &inData[0x08], decSize, decBuffer);
	if (outSize != decSize)
		printf("Warning - not all data was decompressed!\n");
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		free(decBuffer);
		printf("Error writing %s!\n", fileName);
		return;
	}
	fwrite(decBuffer, 1, outSize, hFile);
	fclose(hFile);
	free(decBuffer);
	
	return;
}

static void DecompressMultiFile(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 curPos;
	UINT32 cmpSize;
	UINT32 fileCnt;
	UINT32 curFile;
	
	// detect number of files
	fileCnt = 0;
	for (curPos = 0x00; curPos < arcSize; fileCnt ++)
	{
		cmpSize = ReadUInt32(&arcData[curPos + 0x00]);
		curPos += 0x08 + cmpSize;
	}
	//printf("Detected %u %s.\n", fileCnt, (fileCnt == 1) ? "file" : "files");
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	curPos = 0x00;
	for (curFile = 0; curFile < fileCnt; curFile ++)
	{
		cmpSize = ReadUInt32(&arcData[curPos + 0x00]);
		
		// generate file name(ABC.ext -> ABC_00.ext)
		if (fileCnt > 1)
			sprintf(outExt, "_%02X%s", curFile, fileExt);
		
		printf("File %u / %u: offset: 0x%06X\n    ", 1 + curFile, fileCnt, curPos);
		DecompressFile(&arcData[curPos], outName);
		curPos += 0x08 + cmpSize;
	}
	
	return;
}

static void ExtractArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	FILE* hFile;
	UINT32 curPos;
	UINT32 filePos;
	UINT32 fileSize;
	UINT32 fileCnt;
	UINT32 curFile;
	
	fileCnt = ReadUInt16(&arcData[0x00]);
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	curPos = 0x02;
	for (curFile = 0; curFile < fileCnt; curFile ++, curPos += 0x08)
	{
		filePos = ReadUInt32(&arcData[curPos + 0x00]);
		fileSize = ReadUInt32(&arcData[curPos + 0x04]);
		
		// generate file name(ABC.ext -> ABC_00.ext)
		if (fileCnt > 1)
			sprintf(outExt, "_%02X%s", curFile, fileExt);
		
		// The actual file data may or may not be compressed.
		// (Only the game code knows whether or not it is compressed.)
		printf("File %u / %u: offset: 0x%06X, size 0x%04X\n", 1 + curFile, fileCnt, filePos, fileSize);
		hFile = fopen(outName, "wb");
		if (hFile == NULL)
		{
			printf("Error writing %s!\n", outName);
			continue;
		}
		fwrite(&arcData[filePos], 1, fileSize, hFile);
		fclose(hFile);
	}
	
	return;
}

// LZSS decoder by Haruhiko Okumura, 1989-04-06
// modified to work with memory instead of file streams

#define N		 4096	/* size of ring buffer */
#define F		   18	/* upper limit for match_length */
#define THRESHOLD	2	/* encode string into position and length
						   if match_length is greater than this */

static void LZSS_BufInit(UINT8* text_buf)
{
	// Important Note: These are non-standard values and ARE used by the compressed data.
	UINT16 bufPos;
	UINT16 regD0;
	UINT16 regD1;
	
	// LZSS table initialization from Arcus Odyssey X68000, M_DRV.X
	// decompression routine: file offset 0x020C, executed from 0x03593C
	bufPos = 0x0000;
	// 035946 - 000..CFF (0x0D bytes of 00, 01, 02, ... FF each)
	for (regD0 = 0x00; regD0 < 0x100; regD0 ++)
	{
		for (regD1 = 0x00; regD1 < 0x0D; regD1 ++, bufPos ++)
			text_buf[bufPos] = (UINT8)regD0;
	}
	// 035954 - AD00..ADFF (00 .. FF)
	for (regD0 = 0x00; regD0 < 0x100; regD0 ++, bufPos ++)
		text_buf[bufPos] = (UINT8)regD0;
	// 03595A - AE00..AEFF (FF .. 00)
	do
	{
		regD0 --;
		text_buf[bufPos] = (UINT8)regD0;
		bufPos ++;
	} while(regD0 > 0x00);
	// 035960 - AF00..AF7F (0x80 times 00)
	for (regD0 = 0x00; regD0 < 0x80; regD0 ++, bufPos ++)
		text_buf[bufPos] = 0x00;
	// 035968 - AF80..AFED (0x6E times 20/space)
	for (regD0 = 0x00; regD0 < 0x80 - F; regD0 ++, bufPos ++)
		text_buf[bufPos] = ' ';
	
	return;
}

UINT32 LZSS_Decode(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
{
	UINT32 inPos, outPos;
	UINT8 text_buf[N];	/* ring buffer of size N,
			with extra F-1 bytes to facilitate string comparison */
	int  i, j, k, r, c;
	unsigned int  flags;
	
	//for (i = 0; i < N - F; i++) text_buf[i] = 0x00;
	LZSS_BufInit(text_buf);
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

static UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x00] << 0) | (data[0x01] << 8);
}

static UINT16 ReadBE16(const UINT8* data)
{
	return	(data[0x00] << 8) | (data[0x01] << 0);
}

static UINT16 ReadUInt16(const UINT8* data)
{
	return (fmtByteOrder == BO_LE) ? ReadLE16(data) : ReadBE16(data);
}

static UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x00] <<  0) | (data[0x01] <<  8) |
			(data[0x02] << 16) | (data[0x03] << 24);
}

static UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}

static UINT32 ReadUInt32(const UINT8* data)
{
	return (fmtByteOrder == BO_LE) ? ReadLE32(data) : ReadBE32(data);
}
