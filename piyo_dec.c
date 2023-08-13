// PANDA HOUSE 'PIYO' decoder
#include <stdio.h>
#include <stdlib.h>
#include <string.h>	// for memcmp/memcpy

typedef unsigned char UINT8;
typedef unsigned short UINT16;

static UINT16 ReadLE16(const UINT8* data);
static void WriteLE16(UINT8* buffer, UINT16 value);
static void DecodeData(UINT8* dst, const UINT8* src, size_t len, UINT8 keyInit);
static size_t DecodeCOMData(size_t srcLen, UINT8* data);
static size_t DecodeEXEData(size_t srcLen, UINT8* data);


int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	size_t srcLen;
	size_t decLen;
	UINT8* data;
	
	printf("PANDA HOUSE 'PIYO' decoder\n--------------------------\n");
	if (argc < 3)
	{
		printf("Usage: %s MFD.COM MFD_DEC.COM\n", argv[0]);
		printf("Usage: %s MAXG.EXE MAXG_DEC.EXE\n", argv[0]);
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
	if (srcLen > 0x100000)	// 1 MB
		srcLen = 0x100000;
	
	fseek(hFile, 0x00, SEEK_SET);
	data = (UINT8*)malloc(srcLen);
	fread(data, 0x01, srcLen, hFile);
	
	fclose(hFile);
	
	if (data[0x00] == 0xE9)	// 8086 jump instruction
	{
		decLen = DecodeCOMData(srcLen, data);
	}
	else if (data[0x00] == 'M' && data[0x01] == 'Z')	// "MZ" signature
	{
		decLen = DecodeEXEData(srcLen, data);
	}
	else
	{
		printf("Error: Unknown executable type!\n");
	}
	
	if (decLen != (size_t)-1)
	{
		hFile = fopen(argv[argbase + 1], "wb");
		if (hFile == NULL)
		{
			free(data);
			printf("Error opening %s!\n", argv[argbase + 1]);
			return 2;
		}
		fwrite(data, 0x01, decLen, hFile);
		fclose(hFile);
		
		printf("Done.\n");
	}
	
	free(data);
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

static UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

static void WriteLE16(UINT8* buffer, UINT16 value)
{
	buffer[0x00] = (value >> 0) & 0xFF;
	buffer[0x01] = (value >> 8) & 0xFF;
	
	return;
}

static void DecodeData(UINT8* dst, const UINT8* src, size_t len, UINT8 keyInit)
{
	size_t pos;
	UINT8 val;
	UINT8 key = keyInit;
	for (pos = 0x00; pos < len; pos ++)
	{
		val = src[pos];                 //  LODSB
		val = (val << 1) | (val >> 7);  //  ROL     AL, 1
		val ^= key;                     //  XOR     AL, BL
		key += val;                     //  ADD     BL, AL
		dst[pos] = val;                 //  STOSB
	}
	return;
}

static size_t DecodeCOMData(size_t srcLen, UINT8* data)
{
	size_t decLen;
	
	if (0x0A > srcLen || memcmp(&data[0x06], "PIYO", 0x04))
	{
		printf("PIYO signature not found!\n");
		return (size_t)-1;
	}
	
	/* layout of encrypted COM file:
		00..02: [code] instruction "JMP decode"
		03..04: size of data to decode
		  05  : initial value of decode register
		06..09: "PIYO" signature (ignored)
		0A..  : encoded data
	*/
	decLen = ReadLE16(&data[0x03]);
	DecodeData(&data[0x00], &data[0x0A], decLen, data[0x05]);
	
	return decLen;
}

/* MZ EXE file header structure:
	Pos     Len     Description
	00      02      "MZ" signature
	02      02      number of bytes in last 512-byte page
	04      02      number of 512-byte pages (includes partial past page)
	06      02      number of relocation entries
	08      02      header size in 16-byte paragraphs
	0A      02      additional allocation: minimum number of paragraphs
	0C      02      additional allocation: maximum number of paragraphs (often 0FFFFh)
	0E      02      initial value of SS register
	10      02      initial value of SP register
	12      02      checksum (can be 0)
	14      02      initial value of IP register
	16      02      initial value of CS register
	18      02      offset of relocation table
	1A      02      overlay number (usually 0)
*/
static size_t DecodeEXEData(size_t srcLen, UINT8* data)
{
	UINT16 hdrSize = ReadLE16(&data[0x08]);	// in paragraphs of 0x10 bytes
	UINT16 initIP = ReadLE16(&data[0x14]);
	UINT16 initCS = ReadLE16(&data[0x16]);
	size_t baseOfs = hdrSize * 0x10;
	size_t piyoBase = baseOfs + initCS * 0x10 + initIP;
	size_t decLen;
	
	if (piyoBase + 0x19 > srcLen || memcmp(&data[piyoBase + 0x15], "PIYO", 0x04))
	{
		printf("PIYO signature not found!\n");
		return (size_t)-1;
	}
	
	/* layout of decode segment in encrypted EXE file:
		00..04: [code] PUSHF / CLI / CALL base+0005h
		05..07: [code] POP BX / JMP $+17h (jump over PIYO decode block)
		08..09: register CS (code segment) -> MZ offset 16h
		0A..0B: register IP (instruction pointer) -> MZ offset 14h
		0C..0D: register SS (stack segment) -> MZ offset 0Eh
		0E..0F: register SP (stack pointer) -> MZ offset 10h
		10..11: size of data to decode
		12..13: high word of decode size? (unused)
		  14  : initial value of decode register
		15..18: "PIYO" signature (ignored)
		19..1A: number of bytes in last page -> MZ offset 02h
		1B..1C: number of 512-byte pages -> MZ offset 04h
		1D..  : [code] decoding logic
	*/
	
	// decode program data
	decLen = ReadLE16(&data[piyoBase + 0x10]);
	DecodeData(&data[baseOfs], &data[baseOfs], decLen, data[piyoBase + 0x14]);
	
	// copy 8086 register values into MZ header
	memcpy(&data[0x16], &data[piyoBase + 0x08], 0x02);	// copy register CS
	memcpy(&data[0x14], &data[piyoBase + 0x0A], 0x02);	// copy register IP
	memcpy(&data[0x0E], &data[piyoBase + 0x0C], 0x04);	// copy register SS:SP
	memcpy(&data[0x02], &data[piyoBase + 0x19], 0x04);	// copy MZ page info
	
	return srcLen - 0x80;
}
