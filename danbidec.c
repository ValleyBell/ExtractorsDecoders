// Danbi System Decoder
// --------------------
// Valley Bell, written on 2021-12-24
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef unsigned char	UINT8;


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
	
	printf("Danbi System Decoder\n--------------------\n");
	if (argc < 3)
	{
		printf("Usage: %s input.bin output.bin\n", argv[0]);
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
	
	{
		size_t curPos;
		for (curPos = 0x00; curPos < inLen; curPos ++)
			inData[curPos] ^= (curPos & 0xFF);
		
		hFile = fopen(argv[argbase + 1], "wb");
		if (hFile == NULL)
		{
			printf("Error writing %s!\n", argv[argbase + 1]);
		}
		else
		{
			fwrite(inData, 1, inLen, hFile);
			fclose(hFile);
		}
	}
	
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
