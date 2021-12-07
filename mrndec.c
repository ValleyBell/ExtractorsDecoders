// Mirinae Software Decompressor
// -----------------------------
// Valley Bell, written on 2021-12-07
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAVE_STDINT

#include <stdint.h>
typedef uint8_t	UINT8;
typedef uint16_t	UINT16;
typedef  int16_t	 INT16;

#else	// ! HAVE_STDINT

typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef   signed short	 INT16;

#endif	// HAVE_STDINT


static void DecompressFile(size_t inSize, const UINT8* inData, const char* fileName);
static UINT16 ReadLE16(const UINT8* data);


static UINT8 decodeKey = 0x6B;
static size_t songCnt = 20;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	size_t inLen;
	UINT8* inData;
#ifdef _WIN32
	FILETIME ftWrite;
	HANDLE hWinFile;
#endif
	
	printf("Mirinae Software Decompressor\n-----------------------------\n");
	if (argc < 2)
	{
		printf("Usage: %s compressed.bin decompressed.bin\n");
		return 0;
	}
	argbase = 1;
	/*while(argbase < argc && argv[argbase][0] == '-')
	{
		if (argv[argbase][1] == '\0')
		{
		}
		else
			break;
		argbase ++;
	}*/
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
	
#ifdef _WIN32
	ftWrite.dwLowDateTime = ftWrite.dwHighDateTime = 0;
	hWinFile = CreateFile(argv[argbase + 0], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hWinFile != INVALID_HANDLE_VALUE)
	{
		GetFileTime(hWinFile, NULL, NULL, &ftWrite);
		CloseHandle(hWinFile);
	}
#endif
	
	DecompressFile(inLen, inData, argv[argbase + 1]);
	
#ifdef _WIN32
	if (ftWrite.dwLowDateTime != 0 && ftWrite.dwHighDateTime != 0)
	{
		hWinFile = CreateFile(argv[argbase + 1], GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hWinFile != INVALID_HANDLE_VALUE)
		{
			SetFileTime(hWinFile, NULL, NULL, &ftWrite);
			CloseHandle(hWinFile);
		}
	}
#endif
	
	free(inData);
	
	return 0;
}

static void DecompressFile(size_t inSize, const UINT8* inData, const char* fileName)
{
	FILE* hFile;
	UINT8* decBuf;
	size_t inPos;
	size_t outPos;
	UINT16 ctrlData;	// reg BP
	UINT8 ctrlBits;		// reg DL
	UINT8 carry;
	UINT16 copyCnt;	// reg CX
	INT16 copyOfs;	// reg BX
	
	decBuf = (UINT8*)malloc(0x10000);	// 64 KB for now
	
	inPos = 0x00;
	outPos = 0x00;
	
	ctrlData = ReadLE16(&inData[inPos]);	inPos += 0x02;
	ctrlBits = 16;
	copyCnt = 0;
	while(inPos < inSize && outPos < 0x10000)
	{
		//loc_10C44
		carry = (ctrlData & 0x01);
		ctrlData >>= 1;
		ctrlBits --;
		if (ctrlBits == 0)
		{
			ctrlData = ReadLE16(&inData[inPos]);	inPos += 0x02;
			ctrlBits = 16;
		}
		if (carry)
		{
			decBuf[outPos] = inData[inPos];
			inPos ++;	outPos ++;
			continue;
		}
		
		//loc_10C54
		carry = (ctrlData & 0x01);
		ctrlData >>= 1;
		ctrlBits --;
		if (ctrlBits == 0)
		{
			ctrlData = ReadLE16(&inData[inPos]);	inPos += 0x02;
			ctrlBits = 16;
		}
		//loc_10C5F
		if (! carry)
		{
			carry = (ctrlData & 0x01);
			ctrlData >>= 1;
			ctrlBits --;
			if (ctrlBits == 0)
			{
				ctrlData = ReadLE16(&inData[inPos]);	inPos += 0x02;
				ctrlBits = 16;
			}
			//loc_10C6C
			copyCnt = carry;
			
			carry = (ctrlData & 0x01);
			ctrlData >>= 1;
			ctrlBits --;
			if (ctrlBits == 0)
			{
				ctrlData = ReadLE16(&inData[inPos]);	inPos += 0x02;
				ctrlBits = 16;
			}
			//loc_10C79
			copyCnt = (copyCnt << 1) | carry;
			
			copyCnt += 2;
			copyOfs = -0x100 + inData[inPos];	inPos += 0x01;
		}
		else
		{
			//loc_10C84
			UINT16 ax = ReadLE16(&inData[inPos]);	inPos += 0x02;
			copyOfs = -0x2000 + (ax & 0x1FFF);
			copyCnt = (ax >> 13);
			if (copyCnt != 0)
			{
				//loc_10C9E
				copyCnt = copyCnt + 2;
			}
			else
			{
				UINT8 cmd;	// reg AL
				cmd = inData[inPos];	inPos += 0x01;
				if (cmd == 0)
				{
					//loc_10CAC
					copyCnt = 0;	// segment reset - nothing to do here
					continue;
				}
				else if (cmd == 1)
				{
					break;	// file end
				}
				else
				{
					copyCnt = cmd + 1;
				}
			}
		}
		for (; copyCnt > 0; copyCnt --)
		{
			decBuf[outPos] = decBuf[outPos + copyOfs];
			outPos ++;
		}
	}
	
	printf("%u bytes -> %u bytes.\n", inPos, outPos);
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
	}
	else
	{
		fwrite(decBuf, 1, outPos, hFile);
		fclose(hFile);
	}
	free(decBuf);
	
	return;
}

static UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x00] << 0) | (data[0x01] << 8);
}
