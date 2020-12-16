// PC-98 MFD driver decoder
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char UINT8;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	size_t srcLen;
	size_t decLen;
	UINT8* data;
	size_t curPos;
	UINT8 key;
	
	printf("PC-98 MFD driver decoder\n------------------------\n");
	if (argc < 3)
	{
		printf("Usage: %s MFD.COM MFD_DEC.COM\n", argv[0]);
		return 0;
	}
	
	argbase = 1;
	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	srcLen = ftell(hFile);
	if (srcLen > 0x10000)	// 64 KB
		srcLen = 0x10000;
	
	fseek(hFile, 0x00, SEEK_SET);
	data = (UINT8*)malloc(srcLen);
	fread(data, 0x01, srcLen, hFile);
	
	fclose(hFile);
	
	// 00..02: instruction JMP decode
	// 03..04: size of data to decode
	//   05  : initial value of decode register
	// 06..09: "PIYO" (ignored)
	// 0A..  : encoded data
	decLen = (data[0x03] << 0) | (data[0x04] << 8);
	key = data[0x05];
	for (curPos = 0x00; curPos < decLen; curPos ++)
	{
		UINT8 val = data[0x0A + curPos];    //  LODSB
		val = (val << 1) | (val >> 7);      //  ROL     AL, 1
		val ^= key;                         //  XOR     AL, BL
		key += val;                         //  ADD     BL, AL
		data[curPos] = val;                 //  STOSB
	}
	
	hFile = fopen(argv[argbase + 1], "wb");
	if (hFile == NULL)
	{
		free(data);
		printf("Error opening %s!\n", argv[argbase + 1]);
		return 2;
	}
	fwrite(data, 0x01, decLen, hFile);
	fclose(hFile);
	free(data);
	
	printf("Done.\n");
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}
