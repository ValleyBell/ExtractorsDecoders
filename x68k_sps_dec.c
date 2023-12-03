// X68000 S.P.S. Archive Unpacker
// ------------------------------
// Valley Bell, written on 2017-09-01 (Street Fighter II: Champion Edition)
// updated on 2017-09-02 (Daimakaimura)
// updated on 2017-09-03 (Super Street Fighter II: The New Challengers)
// based on Twinkle Soft Decompressor

// BLK format
// ----------
// Format:
//  repeat N times:
//      4 bytes - file offset
//      4 bytes - file size
//  TOC ends where the first file starts
//  
// Games:
// - Street Fighter II: Champion Edition 
//      C_SE.BLK - uncompressed
//      FM.BLK / GM.BLK - compressed with LZSS_SPS_V1
// - Super Street Fighter II: The New Challengers BLK format
//      FM.BLK / GM.BLK - compressed with LZSS_SPS_V2

// SLD format
// ----------
// Format:
//  repeat N-1 times:
//      2 bytes - size of file 1
//  This is just a list of (n-1) file sizes.
//
// Games:
// - Daimakaimura
//      TEXTDAT2.SLD / TEXTDAT4.SLD - compressed with LZSS_SPS_V1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stdtype.h"


#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
static void DecompressFile(UINT32 inSize, const UINT8* inData, const char* fileName);
static void ExtractBLKArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractSLDArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static UINT32 LZSS_Decode_v1(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT32 LZSS_Decode_v2(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT16 ReadBE16(const UINT8* Data);
static UINT32 ReadBE32(const UINT8* Data);


#define ARC_AUTO		0xFF
#define ARC_BLK			0x00
#define ARC_SLD			0x01

#define LZSS_AUTO		0xFF	// automatic detection
#define LZSS_NONE		0x00	// no compression
#define LZSS_SPS_V1		0x01	// Daimakaimura, Street Fighter II: CE
#define LZSS_SPS_V2		0x02	// Super Street Fighter II: TNC

static const char* ARCHIVE_STRS[] =
{
	"BLK",
	"SLD",
};

static const char* COMPR_STRS[] =
{
	"none",
	"LZSS-SPS v1",
	"LZSS-SPS v2",
};

static UINT8 ArchiveType;
static UINT8 ComprType;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT32 inLen;
	UINT8* inData;
	
	printf("X68000 S.P.S. Archive Unpacker\n------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: x68k_sps_dec.exe [Options] input.blk output.bin\n");
		printf("This will create files output_00.bin, output_01.bin, etc.\n");
		printf("\n");
		printf("Options:\n");
		printf("    -s  SLD archive\n");
		printf("    -b  BLK archive\n");
		printf("    -r  extract raw data\n");
		printf("    -1  decompress using LZSS-SPS variant 1\n");
		printf("    -2  decompress using LZSS-SPS variant 2\n");
		printf("\n");
		printf("Supported games:\n");
		printf("    Daimakaimura (SLD archive, LZSS v1)\n");
		printf("    Street Fighter II: Champion Edition (BLK archive, LZSS v1)\n");
		printf("    Super Street Fighter II: The New Challengers (BLK archive, LZSS v2)\n");
		return 0;
	}
	
	ArchiveType = ARC_AUTO;
	ComprType = LZSS_AUTO;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 's')
		{
			ArchiveType = ARC_SLD;
		}
		else if (argv[argbase][1] == 'b')
		{
			ArchiveType = ARC_BLK;
		}
		else if (argv[argbase][1] == 'r')
		{
			ComprType = LZSS_NONE;
		}
		else if (argv[argbase][1] == '1')
		{
			ComprType = LZSS_SPS_V1;
		}
		else if (argv[argbase][1] == '2')
		{
			ComprType = LZSS_SPS_V2;
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
	
	if (ArchiveType == ARC_AUTO)
	{
		const char* fileExt;
		
		fileExt = strrchr(argv[argbase + 0], '.');
		if (fileExt != NULL)
		{
			fileExt ++;
			if (! stricmp(fileExt, "BLK"))
				ArchiveType = ARC_BLK;
			else if (! stricmp(fileExt, "SLD"))
				ArchiveType = ARC_SLD;
		}
	}
	if (ArchiveType == ARC_AUTO)
	{
		printf("Unknown archive type! Please specify the archive type manually\n");
		return 2;
	}
	printf("Archive format: %s\n", ARCHIVE_STRS[ArchiveType]);
	
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
	
	if (ArchiveType == ARC_BLK)
		ExtractBLKArchive(inLen, inData, argv[argbase + 1]);
	else if (ArchiveType == ARC_SLD)
		ExtractSLDArchive(inLen, inData, argv[argbase + 1]);
	
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
	
	if (ComprType == LZSS_NONE)
	{
		WriteFileData(inSize, inData, fileName);
		return;
	}
	
	decSize = 0x100000;	// 1 MB should be more than enough
	//printf("Compressed: %u bytes, decompressed: %u bytes\n", inSize, decSize);
	decBuffer = (UINT8*)malloc(decSize);
	if (ComprType == LZSS_SPS_V1)
		outSize = LZSS_Decode_v1(inSize, inData, decSize, decBuffer);
	else if (ComprType == LZSS_SPS_V2)
		outSize = LZSS_Decode_v2(inSize, inData, decSize, decBuffer);
	else
	{
		memcpy(decBuffer, inData, inSize);
		outSize = inSize;
	}
	if (outSize >= decSize)
		printf("Warning - not all data was decompressed!\n");
	
	WriteFileData(outSize, decBuffer, fileName);
	
	free(decBuffer);	decBuffer = NULL;
	
	return;
}

static void ExtractBLKArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
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
	UINT8 firstByte;
	
	// detect number of files
	fileCnt = 0;
	firstByte = 0xFF;
	minPos = arcSize;
	for (arcPos = 0x00; arcPos < minPos; arcPos += 0x08, fileCnt ++)
	{
		filePos = ReadBE32(&arcData[arcPos + 0x00]);
		if (filePos < minPos)
			minPos = filePos;
		fileSize = ReadBE32(&arcData[arcPos + 0x04]);
		if (filePos + fileSize > arcSize)
			break;
		// Assume that every compressed file starts with an FF byte. (for LZSS SPS v1)
		// (might fail if there are lots of repeated bytes at the beginning)
		if (filePos < arcSize)
			firstByte &= arcData[filePos];
	}
	if (ComprType == LZSS_AUTO)
	{
		if (firstByte == 0xFF)
			ComprType = LZSS_SPS_V1;
		else if (firstByte == 0xFA)	// MIDIs in LZSS SPS v2 begin with this
			ComprType = LZSS_SPS_V2;
		else
			ComprType = LZSS_NONE;
	}
	printf("Compression: %s\n", COMPR_STRS[ComprType]);
	
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
		
		printf("file %u / %u\n", 1 + curFile, fileCnt);
		DecompressFile(fileSize, &arcData[filePos], outName);
	}
	
	return;
}

static void ExtractSLDArchive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
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
	filePos = 0x00;
	for (arcPos = 0x00; arcPos < minPos; arcPos += 0x02, fileCnt ++)
	{
		fileSize = ReadBE16(&arcData[arcPos]);
		filePos += fileSize;
		if (arcPos + filePos >= arcSize)
		{
			fileCnt ++;
			break;
		}
	}
	if (ComprType == LZSS_AUTO)
		ComprType = LZSS_SPS_V1;
	printf("Compression: %s\n", COMPR_STRS[ComprType]);
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = 0x00;
	filePos = (fileCnt - 0x01) * 0x02;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x02)
	{
		if (arcPos < (fileCnt - 0x01) * 0x02)
			fileSize = ReadBE16(&arcData[arcPos]);
		else
			fileSize = arcSize - filePos;
		if (filePos + fileSize > arcSize)
			fileSize = arcSize - filePos;
		
		// generate file name(ABC.ext -> ABC_00.ext)
		sprintf(outExt, "_%02X%s", curFile, fileExt);
		
		printf("file %u / %u\n", 1 + curFile, fileCnt);
		DecompressFile(fileSize, &arcData[filePos], outName);
		filePos += fileSize;
	}
	
	return;
}

// original LZSS decoder by Haruhiko Okumura, 1989-04-06
// This is a modified version that doesn't use a ring buffer.
// Instead output data is referenced directly.

static UINT32 LZSS_Decode_v1(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
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
			i |= ((j & 0xf0) << 4);  j = (j & 0x0f) + 2;
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

// custom LZSS variant used in Super Street Fighter II: The New Challengers
// The decompression routine is stored in X68030 RAM at 0919C8-091A30. (decompressed from SP2.X)
static UINT32 LZSS_Decode_v2(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
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
			
			flags <<= 1;  fbits --;
			if (!fbits) {
				flags = inData[inPos++];
				fbits = 8;
			}
			if (! (flags & 0x80))
			{
				// 0919E8
				i = inData[inPos++];
				j = i & 7;
				if (j == 0)
				{
					// 0919F0
					j = inData[inPos++];
					if (j == 0)
						break;	// data end
					j --;
				}
				i = (i & 0xF8) << 5;
				i |= inData[inPos++];
				i = 0x2000 - i;
			}
			else
			{
				// 091A06
				flags <<= 1;  fbits --;
				if (!fbits) {
					flags = inData[inPos++];
					fbits = 8;
				}
				j = (flags & 0x80) >> 5;
				flags <<= 1;  fbits --;
				if (!fbits) {
					flags = inData[inPos++];
					fbits = 8;
				}
				j |= (flags & 0x80) >> 6;
				flags <<= 1;  fbits --;
				if (!fbits) {
					flags = inData[inPos++];
					fbits = 8;
				}
				j |= (flags & 0x80) >> 7;
				j ++;
				i = inData[inPos++];
				i = 0x100 - i;
			}
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

static UINT16 ReadBE16(const UINT8* Data)
{
	return	(Data[0x00] <<  8) | (Data[0x01] <<  0);
}

static UINT32 ReadBE32(const UINT8* Data)
{
	return	(Data[0x00] << 24) | (Data[0x01] << 16) |
			(Data[0x02] <<  8) | (Data[0x03] <<  0);
}
