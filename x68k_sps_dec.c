// X68000 S.P.S. Archive Unpacker
// ------------------------------
// Valley Bell, written on 2017-09-01 (Street Fighter II: Champion Edition)
// updated on 2017-09-02 (Daimakaimura)
// updated on 2017-09-03 (Super Street Fighter II: The New Challengers)
// updated on 2025-07-23 (Final Fight)
// updated on 2025-07-25 (Ajax)
// updated on 2025-07-27 (M2SEQ)
// based on Twinkle Soft Decompressor

// BLK AJX format
// --------------
// Format:
//  repeat N times:
//      2 bytes - start offset of file # (Big Endian)
//  This is just a list of N file offsets.
//
// Note: Some archives seem end the offset list with a pointer to offset 0.
// However this doesn't seem to apply to all archives.
//
// Games:
// - Ajax
//      MUSICS.AJX

// BLK FF format
// -------------
// Format:
//  repeat N times:
//      4 bytes - start offset of file # (Big Endian)
//  This is just a list of N file offsets.
//
// Note: Some archives seem end the offset list with:
//      4 bytes - end-of-archive offset
//      4 bytes - value 0
// However this doesn't seem to apply to all archives.
//
// Games:
// - Final Fight
//      PCM_COMM.BLK / STAGE?.BLK

// BLK SF2 format
// --------------
// Format:
//  repeat N times:
//      4 bytes - file offset (Big Endian)
//      4 bytes - file size (Big Endian)
//  TOC ends where the first file starts
//  
// Games:
// - Street Fighter II: Champion Edition 
//      C_SE.BLK - uncompressed
//      FM.BLK / GM.BLK - compressed with LZSS_SPS_V2
// - Super Street Fighter II: The New Challengers BLK format
//      FM.BLK / GM.BLK - compressed with LZSS_SPS_V3

// SLD FF format
// -------------
// The whole file is compressed with LZSS_SPS_V1 - this includes the archive header.
// Before reading the header, the file has to be decompressed in its entirety first.
// After decompression, which is a BLK v1 archive.
//
// Games:
// - Final Fight
//      BGM.SLD / BGM_MIDI.SLD

// SLD DM format
// -------------
// Format:
//  repeat N times:
//      2 bytes - size of file # (Big Endian)
//  This is just a list of N file sizes.
//  Note: Daimakaimura/TEXTDAT2.SLD seems to have additional data at the end. (It decompresses fine, but is not a MID file.)
//        So this converter tries to extract N+1 files.
//
// Games:
// - Daimakaimura
//      TEXTDAT2.SLD / TEXTDAT4.SLD - compressed with LZSS_SPS_V2

// M2SEQ executables
// -----------------
// These are usual Human68k Xfile executables. They start with a "HU" file signature and the payload data starts at 0x40.
//
// M2SEQ executables have the magic string "M2SEQ" at the beginning of the data.
// The general layout is:
//  - driver code
//  - sequence data (each song consists of a single "track" that uses multiple MIDI channels)
//  - "driver base address"
//  - working RAM
//  - song pointer list
//  - additional code
//  - text strings
//
// The "driver base" offset is loaded using the following 68000 instructions:
//      48E7 080E       MOVEM.L D4/A4-A6, -(SP)
//      4DF9 xxxx xxxx  LEA     $xxxxx.L, A6    ; load driver main offset
//
// Song loading differs per-game:
//  song load: (Marchen Maze)
//      302E 0058       MOVE.W  $58(A6), D0
//      0C40 001E       CMPI.W  #$1E, D0        ; 1E = song count
//      644E            BCC     exit            ; invalid song - exit
//      E548            LSL.W   #2, D0
//      41EE 00AC       LEA     $AC(A6), A0     ; -> offset for music pointer list
//      2870 0000       MOVEA.L (A0,D0.W), A4
//  
//  song load: (Pro Yakyuu World Stadium)
//      102E 0062       MOVE.B  $62(A6), D0
//      0C40 0034       CMPI.W  #$34, D0        ; 34 = song count
//      6400 004E       BCC     exit            ; invalid song - exit
//      E548            LSL.W   #2, D0
//      41EE 00A6       LEA     $A6(A6), A0     ; -> offset for music pointer list
//      2030 0000       MOVE.L  (A0,D0.W), D0
//
// A sort of reliable way for auto-detection this should be to search for "E548 41EE 00" (LSL #2, LEA)
// and expect the song count check (CMP.W) within 0x10 bytes before.
//
// Games:
// - Marchen Maze
//      SEQMM.X
// - Pro Yakyuu World Stadium
//      SEQWS.X


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stdtype.h"


#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


typedef struct _type_name_item
{
	UINT8 type;
	const char* shortName;
	const char* longName;
} TN_ITEM;


static const TN_ITEM* GetNameListByType(const TN_ITEM* tnList, UINT8 type);
static const TN_ITEM* GetNameListByName(const TN_ITEM* tnList, const char* name);
static void PrintShortNameList(const TN_ITEM* tnList);
static void GenerateFileName(char* buffer, const char* fileExt, UINT32 fileNum);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
static void DecompressFile(UINT32 inSize, const UINT8* inData, const char* fileName);
static void FormatDetection(UINT32 arcSize, const UINT8* arcData);
static UINT32 FindPattern2(UINT32 dataLen, const UINT8* data, UINT32 matchLen, const UINT8* matchData, UINT32 startPos);
static void ExtractBLK_AJX_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractBLK_FF_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractBLK_SF2_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractSLD_FF_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractSLD_DM_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static void ExtractM2SEQ_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName);
static UINT32 LZSS_Decode_v1(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT32 LZSS_Decode_v2(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT32 LZSS_Decode_v3(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData);
static UINT16 ReadBE16(const UINT8* Data);
static UINT32 ReadBE32(const UINT8* Data);


#define ARC_AUTO		0xFF
#define ARC_BLK_AJX		0x00
#define ARC_BLK_FF		0x01
#define ARC_BLK_SF2		0x02
#define ARC_SLD_FF		0x10
#define ARC_SLD_DM		0x11
#define ARC_M2SEQ		0x20

#define LZSS_AUTO		0xFF	// automatic detection
#define LZSS_NONE		0x00	// no compression
#define LZSS_SPS_V1		0x01	// Final Fight
#define LZSS_SPS_V2		0x02	// Daimakaimura, Street Fighter II: CE
#define LZSS_SPS_V3		0x03	// Super Street Fighter II: TNC

static TN_ITEM ARCHIVE_FMTS[] =
{
	{ARC_AUTO,      "auto",     "auto"},
	{ARC_BLK_AJX,   "BLK-AJX",  "BLK Ajax"},
	{ARC_BLK_FF,    "BLK-FF",   "BLK Final Fight"},
	{ARC_BLK_SF2,   "BLK-SF2",  "BLK Street Fighter 2"},
	{ARC_SLD_FF,    "SLD-FF",   "SLD Final Fight"},
	{ARC_SLD_DM,    "SLD-DM",   "SLD Daimakaimura"},
	{ARC_M2SEQ,     "M2SEQ",    "M2system sequencer"},
	{0xFF,          NULL,       NULL},
};

static TN_ITEM COMPR_FMTS[] =
{
	{LZSS_AUTO,     "auto", "auto"},
	{LZSS_NONE,     "none", "none"},
	{LZSS_SPS_V1,   "LS1",  "LZSS-SPS v1",},
	{LZSS_SPS_V2,   "LS2",  "LZSS-SPS v2",},
	{LZSS_SPS_V3,   "LS3",  "LZSS-SPS v3",},
	{0xFF,          NULL,   NULL},
};

static UINT8 ArchiveType = ARC_AUTO;
static UINT8 ComprType = LZSS_AUTO;
static UINT8 ExtractDupes = 0;
static char PatOut_NumType = 'x';
static int PatOut_Base = 0;

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
		printf("This will create files output00.bin, output01.bin, etc.\n");
		printf("\n");
		printf("Options:\n");
		printf("    -f fmt  specify archive format, must be one of:\n");
		printf("            "); PrintShortNameList(ARCHIVE_FMTS); printf("\n");
		printf("    -c fmt  specify compression format, must be one of:\n");
		printf("            "); PrintShortNameList(COMPR_FMTS); printf("\n");
		printf("    -d      extract duplicate files\n");
		printf("    -p N#   pattern mode for output file names\n");
		printf("            N = number type: 'd' (decimal) / 'x' (hexadecimal, default)\n");
		printf("            # = counting base: 0 or 1\n");
		printf("    Name matching is case insensitive.\n");
		printf("\n");
		printf("Supported games:\n");
		printf("    Ajax (BLK-AJX archive, uncompressed)\n");
		printf("    Daimakaimura (SLD-DM archive, LZSS v2)\n");
		printf("    Final Fight (SLD-FF/BLK-FF archive, LZSS v1)\n");
		printf("    Street Fighter II: Champion Edition (BLK-SF2 archive, LZSS v2)\n");
		printf("    Super Street Fighter II: The New Challengers (BLK-SF2 archive, LZSS v3)\n");
		return 0;
	}
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == 'f')
		{
			argbase ++;
			if (argbase < argc)
			{
				const TN_ITEM* tni = GetNameListByName(ARCHIVE_FMTS, argv[argbase]);
				if (tni == NULL)
				{
					printf("Unknown archive format: %s\n", argv[argbase]);
					return 1;
				}
				ArchiveType = tni->type;
			}
		}
		else if (argv[argbase][1] == 'c')
		{
			argbase ++;
			if (argbase < argc)
			{
				const TN_ITEM* tni = GetNameListByName(COMPR_FMTS, argv[argbase]);
				if (tni == NULL)
				{
					printf("Unknown compression type: %s\n", argv[argbase]);
					return 1;
				}
				ComprType = tni->type;
			}
		}
		else if (argv[argbase][1] == 'd')
		{
			ExtractDupes = 1;
		}
		else if (argv[argbase][1] == 'p')
		{
			argbase ++;
			if (argbase < argc)
			{
				char* endPtr = NULL;
				char numType = argv[argbase][0];
				int numBase;
				if (numType == 'd' || numType == 'D' || numType == 'x' || numType == 'X')
				{
					PatOut_NumType = numType;
				}
				else
				{
					printf("Invalid number type: %c\n", numType);
					return 1;
				}
				numBase = (int)strtol(&argv[argbase][1], &endPtr, 10);
				if (endPtr == &argv[argbase][1])
				{
					printf("Invalid counting base: %s\n", &argv[argbase][1]);
					return 1;
				}
				PatOut_Base = numBase;
			}
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
	{
		printf("Error opening %s!\n", argv[argbase + 0]);
		return 1;
	}
	
	fseek(hFile, 0, SEEK_END);
	inLen = ftell(hFile);
	if (inLen > 0x100000)
		inLen = 0x100000;	// limit to 1 MB
	
	inData = (UINT8*)malloc(inLen);
	fseek(hFile, 0, SEEK_SET);
	fread(inData, 0x01, inLen, hFile);
	
	fclose(hFile);
	
	if (ArchiveType == ARC_AUTO)
		FormatDetection(inLen, inData);	// will adjust ArchiveType according to the detection
	if (ArchiveType == ARC_AUTO)
	{
		printf("Unknown archive type! Please specify the archive type manually\n");
		free(inData);
		return 2;
	}
	printf("Archive format: %s\n", GetNameListByType(ARCHIVE_FMTS, ArchiveType)->longName);
	
	switch(ArchiveType)
	{
	case ARC_BLK_AJX:
		ExtractBLK_AJX_Archive(inLen, inData, argv[argbase + 1]);
		break;
	case ARC_BLK_FF:
		ExtractBLK_FF_Archive(inLen, inData, argv[argbase + 1]);
		break;
	case ARC_BLK_SF2:
		ExtractBLK_SF2_Archive(inLen, inData, argv[argbase + 1]);
		break;
	case ARC_SLD_FF:
		ExtractSLD_FF_Archive(inLen, inData, argv[argbase + 1]);
		break;
	case ARC_SLD_DM:
		ExtractSLD_DM_Archive(inLen, inData, argv[argbase + 1]);
		break;
	case ARC_M2SEQ:
		ExtractM2SEQ_Archive(inLen, inData, argv[argbase + 1]);
		break;
	}
	
	free(inData);
	
	return 0;
}

static const TN_ITEM* GetNameListByType(const TN_ITEM* tnList, UINT8 type)
{
	for (; tnList->shortName != NULL; tnList ++)
	{
		if (tnList->type == type)
			return tnList;
	}
	return NULL;
}

static const TN_ITEM* GetNameListByName(const TN_ITEM* tnList, const char* name)
{
	for (; tnList->shortName != NULL; tnList ++)
	{
		if (!stricmp(tnList->shortName, name))
			return tnList;
	}
	return NULL;
}

static void PrintShortNameList(const TN_ITEM* tnList)
{
	if (tnList == NULL)
		return;
	
	printf("%s", tnList->shortName);	tnList ++;
	for (; tnList->shortName != NULL; tnList ++)
		printf(", %s", tnList->shortName);
	return;
}

static void GenerateFileName(char* buffer, const char* fileExt, UINT32 fileNum)
{
	int outNum = PatOut_Base + (int)fileNum;
	// generate file name (ABC.ext -> ABC00.ext)
	// (using separate sprintf commands to avoid warnings due to user-defined format strings)
	if (PatOut_NumType == 'x')
		sprintf(buffer, "%02x%s", outNum, fileExt);
	else if (PatOut_NumType == 'X')
		sprintf(buffer, "%02X%s", outNum, fileExt);
	else
		sprintf(buffer, "%02d%s", outNum, fileExt);
	
	return;
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
	else if (ComprType == LZSS_SPS_V3)
		outSize = LZSS_Decode_v3(inSize, inData, decSize, decBuffer);
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

static void FormatDetection(UINT32 arcSize, const UINT8* arcData)
{
	// Note: The detections here are sorted by reliability. More reliable ones are checked earlier.
	
	{	// M2SEQ detection
		if (arcSize >= 0x50)
		{
			if (!memcmp(&arcData[0x00], "HU", 2) && !memcmp(&arcData[0x40], "M2SEQ", 5))
			{
				ArchiveType = ARC_M2SEQ;
				return;
			}
		}
	}
	
	{	// BLK v1/v2 detection
		UINT32 val1 = ReadBE32(&arcData[0x00]);
		UINT32 val2 = ReadBE32(&arcData[0x04]);
		UINT32 val3 = ReadBE32(&arcData[0x08]);
		
		// offset 1, size 1, offset 2, ...
		if (val1 + val2 == val3)
		{
			ArchiveType = ARC_BLK_SF2;
			return;
		}
		// offset 1, offset 2, offset 3, ... (strictly monotonically increasing)
		if (val1 < 0x01000 && (val2 > val1 && val2 < 0x1000) && (val3 > val2 && val3 < 0x1000))
		{
			ArchiveType = ARC_BLK_FF;
			return;
		}
	}
	
	{	// SLD v1 detection
		// LZSS compression control bytes
		if (arcData[0x00] == 0x7F && arcData[0x01] == 0xF0)
		{
			ArchiveType = ARC_SLD_FF;
			return;
		}
	}
	
	{	// AJX detection
		UINT32 val1 = ReadBE16(&arcData[0x00]);
		UINT32 val2 = ReadBE16(&arcData[0x02]);
		UINT32 val3 = ReadBE16(&arcData[0x04]);
		
		// offset 1, offset 2, offset 3, ... (strictly monotonically increasing)
		if (val1 < 0x0100 && (val2 > val1 && val2 < arcSize) && (val3 > val2 && val3 < arcSize))
		{
			ArchiveType = ARC_BLK_AJX;
			return;
		}
	}
	
	{	// SLD v2 detection
		UINT8 isGood = 1;
		UINT32 curPos;
		for (curPos = 0x00; curPos < 0x10; curPos += 0x02)
		{
			UINT16 ptr = ReadBE16(&arcData[curPos]);
			// list of sizes
			if (ptr > 0x4000 || ptr <= 1)
			{
				isGood = 0;
				break;
			}
		}
		if (isGood)
		{
			ArchiveType = ARC_SLD_DM;
			return;
		}
	}
	
	return;
}

static UINT32 FindPattern2(UINT32 dataLen, const UINT8* data, UINT32 matchLen, const UINT8* matchData, UINT32 startPos)
{
	UINT32 curPos;
	
	if (dataLen < matchLen)
		return (UINT32)-1;
	for (curPos = startPos; curPos < dataLen - matchLen; curPos += 0x02)
	{
		if (! memcmp(data + curPos, matchData, matchLen))
			return curPos;
	}
	
	return (UINT32)-1;
}

static void ExtractBLK_AJX_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 filePos;
	UINT32 fileSize;
	UINT32 fileCnt;
	UINT32 curFile;
	UINT32 arcPos;
	UINT32 dataPos;
	UINT32 lastPos;
	UINT32 tempPos;
	
	// detect number of files
	fileCnt = 0;
	dataPos = arcSize;
	for (arcPos = 0x00; arcPos < dataPos; arcPos += 0x02, fileCnt ++)
	{
		filePos = ReadBE16(&arcData[arcPos + 0x00]);
		if (! filePos)
			break;
		if (filePos < dataPos)
			dataPos = filePos;
	}
	printf("Files: %u\n", fileCnt);
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = 0x00;
	lastPos = 0x00;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x02)
	{
		filePos = ReadBE16(&arcData[arcPos + 0x00]);
		fileSize = 0x00;
		if (filePos)
		{
			for (tempPos = arcPos + 0x02; tempPos < dataPos; tempPos += 0x02)
			{
				// search for next non-zero, non-duplicate pointer in a sparse list
				fileSize = ReadBE16(&arcData[tempPos]);
				if (fileSize && fileSize != filePos)
					break;
			}
			if (fileSize <= filePos || fileSize > arcSize)
				fileSize = arcSize;
			fileSize -= filePos;
		}
		
		GenerateFileName(outExt, fileExt, curFile);
		
		printf("File %u/%u - pos 0x%06X, len 0x%04X", 1 + curFile, fileCnt, filePos, fileSize);
		if (filePos == lastPos && !ExtractDupes)
			printf("    duplicate file - skipping");
		else if (!filePos || filePos > arcSize || (filePos == arcSize && fileSize > 0))
			printf("    Bad start offset - ignoring!");
		else
			WriteFileData(fileSize, &arcData[filePos], outName);
		printf("\n");
		lastPos = filePos;
	}
	
	return;
}

static void ExtractBLK_FF_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 filePos;
	UINT32 fileSize;
	UINT32 fileCnt;
	UINT32 curFile;
	UINT32 arcPos;
	UINT32 dataPos;
	UINT32 lastPos;
	UINT32 tempPos;
	
	// detect number of files
	fileCnt = 0;
	dataPos = arcSize;
	for (arcPos = 0x00; arcPos < dataPos; arcPos += 0x04, fileCnt ++)
	{
		filePos = ReadBE32(&arcData[arcPos + 0x00]);
		if (! filePos)
			break;
		if (filePos < dataPos)
			dataPos = filePos;
	}
	printf("Files: %u\n", fileCnt);
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = 0x00;
	lastPos = 0x00;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x04)
	{
		filePos = ReadBE32(&arcData[arcPos + 0x00]);
		fileSize = 0x00;
		if (filePos)
		{
			for (tempPos = arcPos + 0x04; tempPos < dataPos; tempPos += 0x04)
			{
				// search for next non-zero, non-duplicate pointer in a sparse list
				fileSize = ReadBE32(&arcData[tempPos]);
				if (fileSize && fileSize != filePos)
					break;
			}
			if (fileSize <= filePos || fileSize > arcSize)
				fileSize = arcSize;
			fileSize -= filePos;
		}
		
		GenerateFileName(outExt, fileExt, curFile);
		
		printf("File %u/%u - pos 0x%06X, len 0x%04X", 1 + curFile, fileCnt, filePos, fileSize);
		if (filePos == lastPos && !ExtractDupes)
			printf("    duplicate file - skipping");
		else if (!filePos || filePos > arcSize || (filePos == arcSize && fileSize > 0))
			printf("    Bad start offset - ignoring!");
		else
			WriteFileData(fileSize, &arcData[filePos], outName);
		printf("\n");
		lastPos = filePos;
	}
	
	return;
}

static void ExtractBLK_SF2_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
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
	printf("Files: %u\n", fileCnt);
	if (ComprType == LZSS_AUTO)
	{
		if (firstByte == 0xFF)
			ComprType = LZSS_SPS_V2;
		else if (firstByte == 0xFA)	// MIDIs in LZSS SPS v2 begin with this
			ComprType = LZSS_SPS_V3;
		else
			ComprType = LZSS_NONE;
	}
	printf("Compression: %s\n", GetNameListByType(COMPR_FMTS, ComprType)->longName);
	
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
		
		GenerateFileName(outExt, fileExt, curFile);
		
		printf("File %u/%u - pos 0x%06X, len 0x%04X", 1 + curFile, fileCnt, filePos, fileSize);
		if (!filePos || filePos > arcSize || (filePos == arcSize && fileSize > 0))
			printf("    Bad start offset - ignoring!");
		else
			DecompressFile(fileSize, &arcData[filePos], outName);
		printf("\n");
	}
	
	return;
}

static void ExtractSLD_FF_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	UINT32 decSize;
	UINT8* decBuffer;
	UINT32 outSize;
	
	printf("Compression: %s\n", GetNameListByType(COMPR_FMTS, LZSS_SPS_V1)->longName);
	decSize = arcSize * 8;
	decBuffer = (UINT8*)malloc(decSize);
	outSize = LZSS_Decode_v1(arcSize, arcData, decSize, decBuffer);
	if (outSize >= decSize)
		printf("Warning - not all data was decompressed!\n");
	
	ExtractBLK_FF_Archive(outSize, decBuffer, fileName);
	
	free(decBuffer);	decBuffer = NULL;
	
	return;
}

static void ExtractSLD_DM_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
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
		if (arcPos + filePos + fileSize > arcSize)
		{
			minPos = arcPos;
			break;
		}
		filePos += fileSize;
	}
	if (arcPos + filePos < arcSize)
		fileCnt ++;	// try to also extract additional data
	printf("Files: %u\n", fileCnt);
	
	if (ComprType == LZSS_AUTO)
		ComprType = LZSS_SPS_V2;
	printf("Compression: %s\n", GetNameListByType(COMPR_FMTS, ComprType)->longName);
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = 0x00;
	filePos = minPos;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x02)
	{
		if (arcPos < (fileCnt - 0x01) * 0x02)
			fileSize = ReadBE16(&arcData[arcPos]);
		else
			fileSize = arcSize - filePos;
		if (filePos + fileSize > arcSize)
			fileSize = arcSize - filePos;
		
		GenerateFileName(outExt, fileExt, curFile);
		
		printf("File %u/%u - pos 0x%06X, len 0x%04X", 1 + curFile, fileCnt, filePos, fileSize);
		if (filePos > arcSize || (filePos == arcSize && fileSize > 0))
			printf("    Bad start offset - ignoring!");
		else
			DecompressFile(fileSize, &arcData[filePos], outName);
		printf("\n");
		filePos += fileSize;
	}
	
	return;
}

static void ExtractM2SEQ_Archive(UINT32 arcSize, const UINT8* arcData, const char* fileName)
{
	static const UINT8 MAGIC_DRVBASE[] = {0x48, 0xE7, 0x08, 0x0E, 0x4D, 0xF9};
	static const UINT8 MAGIC_SONGLOAD[] = {0xE5, 0x48, 0x41, 0xEE, 0x00};
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 drvBase;
	UINT32 songLoadPos;
	UINT32 tocPos;
	UINT32 filePos;
	UINT32 fileSize;
	UINT32 fileCnt;
	UINT32 curFile;
	UINT32 arcPos;
	UINT32 endPos;
	UINT32 lastPos;
	UINT32 tempPos;
	
	if (arcSize < 0x40)
		return;
	arcData = &arcData[0x40];	arcSize -= 0x40;	// strip Human68k Xfile header
	
	drvBase = FindPattern2(arcSize, arcData, sizeof(MAGIC_DRVBASE), MAGIC_DRVBASE, 0x0000);
	if (drvBase == (UINT32)-1)
	{
		printf("Driver base offset not found!\n");
		return;
	}
	drvBase = ReadBE32(&arcData[drvBase + 0x06]);
	songLoadPos = FindPattern2(arcSize, arcData, sizeof(MAGIC_SONGLOAD), MAGIC_SONGLOAD, 0x0000);
	if (songLoadPos == (UINT32)-1)
	{
		printf("Song list not found!\n");
		return;
	}
	tocPos = drvBase + ReadBE16(&arcData[songLoadPos + 0x04]);
	printf("Song list offset: 0x%04X\n", tocPos);
	
	fileCnt = (UINT32)-1;
	for (tempPos = songLoadPos - 0x02; tempPos > 0 && tempPos >= songLoadPos - 0x10; tempPos -= 0x02)
	{
		if (ReadBE16(&arcData[tempPos]) == 0x0C40)
		{
			fileCnt = ReadBE16(&arcData[tempPos + 0x02]);
			break;
		}
	}
	if (fileCnt == (UINT32)-1)
	{
		UINT32 lastPtr = 0x00;
		
		printf("Song list size not found - falling back to list size detection.\n");
		// detect number of files
		fileCnt = 0;
		for (arcPos = tocPos; arcPos < arcSize; arcPos += 0x04, fileCnt ++)
		{
			filePos = ReadBE32(&arcData[arcPos + 0x00]);
			if (filePos >= drvBase)
				break;
			if (filePos)
			{
				if (filePos < lastPtr)
					break;
				lastPtr = filePos;
			}
		}
	}
	printf("Files: %u\n", fileCnt);
	
	fileExt = strrchr(fileName, '.');
	if (fileExt == NULL)
		fileExt = fileName + strlen(fileName);
	outName = (char*)malloc(strlen(fileName) + 0x10);
	strcpy(outName, fileName);
	outExt = outName + (fileExt - fileName);
	
	// extract everything
	arcPos = tocPos;
	endPos = tocPos + fileCnt * 0x04;
	lastPos = 0x00;
	for (curFile = 0; curFile < fileCnt; curFile ++, arcPos += 0x04)
	{
		filePos = ReadBE32(&arcData[arcPos + 0x00]);
		fileSize = 0x00;
		if (filePos)
		{
			for (tempPos = arcPos + 0x04; tempPos < endPos; tempPos += 0x04)
			{
				// search for next non-zero, non-duplicate pointer in a sparse list
				fileSize = ReadBE32(&arcData[tempPos]);
				if (fileSize && fileSize != filePos)
					break;
			}
			if (fileSize <= filePos || fileSize > drvBase)
				fileSize = drvBase;
			fileSize -= filePos;
		}
		
		GenerateFileName(outExt, fileExt, curFile);
		
		printf("File %u/%u - pos 0x%06X, len 0x%04X", 1 + curFile, fileCnt, filePos, fileSize);
		if (filePos == lastPos && !ExtractDupes)
			printf("    duplicate file - skipping");
		else if (!filePos || filePos > arcSize || (filePos == arcSize && fileSize > 0))
			printf("    Bad start offset - ignoring!");
		else
			WriteFileData(fileSize, &arcData[filePos], outName);
		printf("\n");
		lastPos = filePos;
	}
	
	return;
}

static UINT32 LZSS_Decode_v1(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
{
	UINT32 inPos, outPos;
	unsigned int i, j, k, r;
	unsigned int flags, fbits;
	UINT8 text_buf[0x1000];
	
	memset(text_buf, 0x00, 0x1000);
	r = 0xFEE;  flags = 0;  fbits = 1;
	inPos = outPos = 0;
	while(inPos < inLen && outPos < outLen) {
		flags <<= 1;  fbits --;
		if (!fbits) {
			flags = inData[inPos++];
			fbits = 8;
		}
		if (flags & 0x80) {
			if (inPos >= inLen) break;
			outData[outPos++] = text_buf[r++] = inData[inPos++];
			r &= 0xFFF;
		} else {
			if (inPos + 1 >= inLen) break;
			j = inData[inPos++];
			i = inData[inPos++];
			i |= ((j & 0xf0) << 4);  j = (j & 0x0f) + 2;
			for (k = 0; k <= j; k++) {
				UINT8 c;
				if (outPos >= outLen) break;
				c = text_buf[(i + k) & 0xFFF];
				outData[outPos++] = text_buf[r++] = c;
				r &= 0xFFF;
			}
		}
	}
	return outPos;
}

// original LZSS decoder by Haruhiko Okumura, 1989-04-06
// This is a modified version that doesn't use a ring buffer.
// Instead output data is referenced directly.

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
static UINT32 LZSS_Decode_v3(UINT32 inLen, const UINT8* inData, UINT32 outLen, UINT8* outData)
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
