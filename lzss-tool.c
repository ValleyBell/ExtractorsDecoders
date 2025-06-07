/* LZSS compression and decompression tool
   Written by Valley Bell, 2024-02
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lzss-lib.h"

#ifdef _MSC_VER
#define strdup		_strdup
#endif


typedef struct archive_header_value
{
	char type;
	union
	{
		struct
		{
			size_t len;
			const uint8_t* ptr;
		} str;
		uint8_t dataVal;
		struct
		{
			uint8_t bytes;		// value size (number of bytes)
			uint8_t endianess;	// see ENDIAN_* constants
			uint8_t set_val;	// set to 1 for the item that sets values
		} size;
	} d;
} ARC_HDR_VAL;
#define ENDIAN_LITTLE	0
#define ENDIAN_BIG		1
typedef struct archive_header_specification
{
	size_t len;
	size_t count;
	ARC_HDR_VAL vals[0x10];
} ARC_HDR_SPEC;

typedef struct file_data
{
	size_t len;
	uint8_t* data;
} FILE_DATA;

#define MODE_NONE	0x00
#define MODE_ENCODE	0x01
#define MODE_DECODE	0x02


static void PrintHelp(const char* appName);
static void ParseHeaderSpec(const char* spec, ARC_HDR_SPEC* header);
static size_t WriteArchiveHeader(FILE_DATA* hdrData, const ARC_HDR_SPEC* hdrSpec, size_t decSize, size_t cmpSize);
static uint8_t ReadArchiveHeader(const FILE_DATA* hdrData, const ARC_HDR_SPEC* hdrSpec, size_t* decSize, size_t* cmpSize);


int main(int argc, char *argv[])
{
	FILE_DATA inFile;
	FILE_DATA outFile;
	ARC_HDR_SPEC arcHdrSpec;
	uint8_t ret;
	FILE* fp;
	LZSS_CFG cfg;
	LZSS_COMPR* lzss;
	int argbase;
	uint8_t mode;

	lzssGetDefaultConfig(&cfg);
	argbase = 1;
	mode = MODE_NONE;
	arcHdrSpec.len = 0x00;
	arcHdrSpec.count = 0;
	while(argbase < argc)
	{
		char* endptr;
		long val;
		if (!strcmp(argv[argbase], "-h"))	// help
		{
			PrintHelp(argv[0]);
			return 0;
		}
		else if (!strcmp(argv[argbase], "-e"))	// encode
		{
			mode = MODE_ENCODE;
		}
		else if (!strcmp(argv[argbase], "-d"))	// decode
		{
			mode = MODE_DECODE;
		}
		else if (!strcmp(argv[argbase], "-a"))	// archive header
		{
			argbase ++;
			if (argbase >= argc)
			{
				fprintf(stderr, "Insufficient arguments.\n");
				return 1;
			}
			ParseHeaderSpec(argv[argbase], &arcHdrSpec);
		}
		else if (!strcmp(argv[argbase], "-n"))	// name table initialization value
		{
			argbase ++;
			if (argbase >= argc)
			{
				fprintf(stderr, "Insufficient arguments.\n");
				return 1;
			}
			val = strtol(argv[argbase], &endptr, 0);
			if (argv[argbase][0] == 'n')
			{
				cfg.nameTblType = LZSS_NTINIT_NONE;
			}
			else if (argv[argbase][0] == 'p')
			{
				cfg.nameTblType = LZSS_NTINIT_FUNC;
				cfg.nameTblFunc = lzssNameTbl_CommonPatterns;
				cfg.ntFuncParam = NULL;
			}
			else if (endptr != argv[argbase])
			{
				cfg.nameTblType = LZSS_NTINIT_VALUE;
				cfg.nameTblValue = (uint8_t)val;
			}
			else
			{
				fprintf(stderr, "Unknown name table initialization parameter: %s\n", argv[argbase]);
				return 1;
			}
		}
		else if (!strcmp(argv[argbase], "-C"))	// control word bit order
		{
			argbase ++;
			if (argbase >= argc)
			{
				fprintf(stderr, "Insufficient arguments.\n");
				return 1;
			}
			val = strtol(argv[argbase], &endptr, 0);
			if (endptr != argv[argbase])
			{
				cfg.flags &= ~LZSS_FLAGS_CTRLMASK;
				cfg.flags |= val ? LZSS_FLAGS_CTRL_M : LZSS_FLAGS_CTRL_L;
			}
		}
		else if (!strcmp(argv[argbase], "-R"))	// nametable reference value
		{
			argbase ++;
			if (argbase >= argc)
			{
				fprintf(stderr, "Insufficient arguments.\n");
				return 1;
			}
			val = strtol(argv[argbase], &endptr, 0);
			if (endptr != argv[argbase])
			{
				cfg.flags &= ~(LZSS_FLAGS_MTCH_EMASK | LZSS_FLAGS_MTCH_LMASK);
				cfg.flags |= (uint8_t)(val << 4);
			}
		}
		else if (!strcmp(argv[argbase], "-O"))	// start offset for writing to name table buffer
		{
			argbase ++;
			if (argbase >= argc)
			{
				fprintf(stderr, "Insufficient arguments.\n");
				return 1;
			}
			val = strtol(argv[argbase], &endptr, 0);
			if (endptr != argv[argbase])
			{
				cfg.nameTblStartOfs = (int)val;
			}
		}
		else if (!strcmp(argv[argbase], "-E"))	// end-of-stream mode
		{
			argbase ++;
			if (argbase >= argc)
			{
				fprintf(stderr, "Insufficient arguments.\n");
				return 1;
			}
			val = strtol(argv[argbase], &endptr, 0);
			if (endptr != argv[argbase])
			{
				cfg.eosMode = (uint8_t)val;
			}
		}
		else
		{
			break;
		}
		argbase ++;
	}

	if (argc < argbase + 2)
	{
		PrintHelp(argv[0]);
		return 1;
	}
	if (mode == MODE_NONE)
	{
		fprintf(stderr, "No mode specified!\n");
		return 1;
	}

	fp = fopen(argv[argbase + 0], "rb");
	if (fp == NULL)
	{
		fprintf(stderr, "Error opening input file: %s\n", argv[argbase + 0]);
		return 2;
	}
	fseek(fp, 0, SEEK_END);
	inFile.len = ftell(fp);
	rewind(fp);
	inFile.data = (uint8_t*)malloc(inFile.len);
	fread(inFile.data, 1, inFile.len, fp);
	fclose(fp);

	lzss = lzssCreate(&cfg);
	if (mode == MODE_ENCODE)
	{
		size_t encDataSize;
		size_t dataOfs = arcHdrSpec.len;
		outFile.len = dataOfs + inFile.len + inFile.len / 8 + 1;
		outFile.data = (uint8_t*)malloc(outFile.len);

		ret = lzssEncode(lzss, outFile.len - dataOfs, &outFile.data[dataOfs], &encDataSize, inFile.len, inFile.data);
		fprintf(stderr, "In : %u bytes\n", inFile.len);
		fprintf(stderr, "Out: %u bytes\n", encDataSize);
		fprintf(stderr, "Ratio: %.2f %%\n", (double)encDataSize / inFile.len * 100.0);

		WriteArchiveHeader(&outFile, &arcHdrSpec, inFile.len, encDataSize);
		outFile.len = dataOfs + encDataSize;
	}
	else //if (mode == MODE_DECODE)
	{
		size_t cmpSize = inFile.len;
		size_t dataOfs = arcHdrSpec.len;
		outFile.len = inFile.len * 8;	// assume compression up to 12.5%
		ret = ReadArchiveHeader(&inFile, &arcHdrSpec, &outFile.len, &cmpSize);
		if (ret)
		{
			fprintf(stderr, "Header parsing error!\n");
			return 4;
		}
		if (inFile.len > dataOfs + cmpSize)
			inFile.len = dataOfs + cmpSize;
		outFile.data = (uint8_t*)malloc(outFile.len);
		ret = lzssDecode(lzss, outFile.len, outFile.data, &outFile.len, inFile.len - dataOfs, &inFile.data[dataOfs]);
	}
	if (ret != LZSS_ERR_OK)
		fprintf(stderr, "LZSS error code %u after writing %u bytes.\n", ret, (unsigned)outFile.len);

	fp = fopen(argv[argbase + 1], "wb");
	if (fp == NULL)
	{
		fprintf(stderr, "Error opening output file: %s\n", argv[argbase + 1]);
		return 3;
	}
	fwrite(outFile.data, 1, outFile.len, fp);
	fclose(fp);

	return 0;
}

static void PrintHelp(const char* appName)
{
	fprintf(stderr, "Usage: %s [mode/options] input.bin output.bin\n", appName);
	fprintf(stderr, "\n");
	fprintf(stderr, "Mode: (required)\n");
	fprintf(stderr, "    -h    show this help screen\n");
	fprintf(stderr, "    -e    encode / compress\n");
	fprintf(stderr, "    -d    decode / decompress\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "File format options:\n");
	fprintf(stderr, "    -a .. add/read archive header (list of comma-separated values)\n");
	fprintf(stderr, "              n   - none, don't add any header [default]\n");
	fprintf(stderr, "              oNE - original size\n");
	fprintf(stderr, "              cNE - compressed size\n");
	fprintf(stderr, "                     N = size of the value in bytes (2/4)\n");
	fprintf(stderr, "                     E = endianess (L = little [default], B = big) [optional]\n");
	fprintf(stderr, "              sABC - string \"ABC\"\n");
	fprintf(stderr, "              bXX  - byte XX (hexadecimal value)\n");
	fprintf(stderr, "              iXX  - ignored (XX is optional and used for writing)\n");
	fprintf(stderr, "          Example: -a sLZS,b1A,c2B,o4\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Compression options:\n");
	fprintf(stderr, "    -n n  name table initialization value (0x00..0xFF, default: 0x20/space)\n");
	fprintf(stderr, "          special values:\n");
	fprintf(stderr, "              n - none (prevent lookup to data before beginning of the file)\n");
	fprintf(stderr, "              p - various patterns (commonly used by Japanese developers)\n");
	fprintf(stderr, "    -C n  control word bit order (0 = low->high [default], 1 = high->low)\n");
	fprintf(stderr, "    -R n  reference word format (bit mask, default: 0x01)\n");
	fprintf(stderr, "          mask 0x03: nibble position (0 = highest .. 3 = lowest)\n");
	fprintf(stderr, "          mask 0x04: byte endianess (0 = Little Endian, 4 = Big Endian)\n");
	fprintf(stderr, "    -O n  offset where name table buffer starts getting written to\n");
	fprintf(stderr, "          (range: 0x000..0xFFF, default: 0xFEE)\n");
	fprintf(stderr, "    -E n  end-of-stream mode (0 = no EOS marker, 1 = end with null-reference)\n");
	return;
}

static void ChooseHeaderSetVal(ARC_HDR_SPEC* header, char item_type)
{
	size_t lastSetItem = (size_t)-1;
	size_t lastSize = 0;
	size_t hdrItem;

	for (hdrItem = 0; hdrItem < header->count; hdrItem++)
	{
		ARC_HDR_VAL* ahv = &header->vals[hdrItem];
		if (ahv->type != item_type)
			continue;
		if (ahv->type != 'o' && ahv->type != 'c')
			continue;
		ahv->d.size.set_val = 0;
		if (ahv->d.size.bytes >= lastSize)
		{
			lastSetItem = hdrItem;
			lastSize = ahv->d.size.bytes;
		}
	}
	if (lastSetItem != (size_t)-1)
		header->vals[lastSetItem].d.size.set_val = 1;
	return;
}

static void ParseHeaderSpec(const char* spec, ARC_HDR_SPEC* header)
{
	char* spec_str;
	char* spec_ptr;
	char* endptr;

	header->len = 0x00;
	header->count = 0;
	if (*spec == '\0')
		return;

	spec_str = strdup(spec);
	spec_ptr = spec_str;
	while (spec_ptr != NULL && *spec_ptr != '\0' && header->count < 0x10)
	{
		ARC_HDR_VAL* ahv = &header->vals[header->count];
		char mode = *spec_ptr;
		char* sep_ptr = strchr(spec_ptr, ',');
		if (sep_ptr != NULL)
			*sep_ptr = '\0';
		spec_ptr += 1;

		if (mode == 'n')
		{
			header->count = 0;
			break;
		}
		else if (mode == 'o' || mode == 'c')
		{
			size_t param_len = strlen(spec_ptr);
			uint8_t is_good = 1;
			ahv->type = mode;
			ahv->d.size.bytes = 4;
			ahv->d.size.endianess = ENDIAN_LITTLE;
			ahv->d.size.set_val = 0;

			// parse value size digit
			if (param_len >= 1)
			{
				if (spec_ptr[0] >= '0' && spec_ptr[0] <= '9')
				{
					ahv->d.size.bytes = spec_ptr[0] - '0';
					if (!(ahv->d.size.bytes == 2 || ahv->d.size.bytes == 4))
					{
						fprintf(stderr, "Archive header mode %c: Value size is %u, but can only be 2 or 4.\n",
							mode, ahv->d.size.bytes);
						is_good = 0;
					}
				}
				else
				{
					fprintf(stderr, "Archive header mode %c: Invalid value size: %c\n", mode, spec_ptr[0]);
					is_good = 0;
				}
			}
			// parse endianess
			if (param_len >= 2)
			{
				if (spec_ptr[1] == 'L' || spec_ptr[1] == 'l')
					ahv->d.size.endianess = ENDIAN_LITTLE;
				else if (spec_ptr[1] == 'B' || spec_ptr[1] == 'b')
					ahv->d.size.endianess = ENDIAN_BIG;
				else
				{
					fprintf(stderr, "Archive header mode %c: Invalid endianess: %c\n", mode, spec_ptr[1]);
					is_good = 0;
				}
			}
			if (is_good)
			{
				header->count ++;
				header->len += ahv->d.size.bytes;
			}
		}
		else if (mode == 's')
		{
			ahv->type = mode;
			ahv->d.str.ptr = spec + (spec_ptr - spec_str);
			ahv->d.str.len = strlen(spec_ptr);
			header->count ++;
			header->len += ahv->d.str.len;
		}
		else if (mode == 'b' || mode == 'i')
		{
			ahv->type = mode;
			ahv->d.dataVal = (uint8_t)strtoul(spec_ptr, &endptr, 0x10);
			if (mode == 'i' && *spec_ptr == '\0')
			{
				ahv->d.dataVal = 0x00;
				header->count ++;
				header->len += 0x01;
			}
			else if (*endptr == '\0' && *spec_ptr != '\0')
			{
				header->count ++;
				header->len += 0x01;
			}
			else
			{
				fprintf(stderr, "Archive header specification: Invalid byte value: %s\n", spec_ptr);
			}
		}
		else
		{
			fprintf(stderr, "Archive header specification: Invalid header value type: %c\n", mode);
		}
		if (sep_ptr == NULL)
			break;
		spec_ptr = sep_ptr + 1;
	}
	free(spec_str);
	ChooseHeaderSetVal(header, 'o');
	ChooseHeaderSetVal(header, 'c');
	return;
}


static size_t ReadLE(const uint8_t* buffer, uint8_t valSize)
{
	size_t pos;
	size_t val = 0x00;
	for (pos = valSize; pos > 0; pos--)
	{
		val <<= 8;
		val |= buffer[pos - 1];
	}
	return val;
}

static void WriteLE(uint8_t* buffer, uint8_t valSize, size_t val)
{
	size_t pos;
	for (pos = 0; pos < valSize; pos++)
	{
		buffer[pos] = val & 0xFF;
		val >>= 8;
	}
	return;
}

static size_t ReadBE(const uint8_t* buffer, uint8_t valSize)
{
	size_t pos;
	size_t val = 0x00;
	for (pos = 0; pos < valSize; pos++)
	{
		val <<= 8;
		val |= buffer[pos];
	}
	return val;
}

static void WriteBE(uint8_t* buffer, uint8_t valSize, size_t val)
{
	size_t pos;
	for (pos = valSize; pos > 0; pos--)
	{
		buffer[pos - 1] = val & 0xFF;
		val >>= 8;
	}
	return;
}

static size_t WriteArchiveHeader(FILE_DATA* hdrData, const ARC_HDR_SPEC* hdrSpec, size_t decSize, size_t cmpSize)
{
	size_t hdrItem;
	size_t pos;
	if (hdrSpec->len == 0)
		return 0x00;

	pos = 0x00;
	for (hdrItem = 0; hdrItem < hdrSpec->count; hdrItem++)
	{
		const ARC_HDR_VAL* ahv = &hdrSpec->vals[hdrItem];
		if (pos >= hdrData->len)
			break;	// should never happen
		switch (ahv->type)
		{
		case 'n':
			hdrData->len = 0x00;
			return 0x00;
		case 'o':
		case 'c':
		{
			size_t val = 0;
			if (pos + ahv->d.size.bytes > hdrData->len)
				break;
			if (ahv->type == 'o')
				val = decSize;
			else //if (ahv->type == 'c')
				val = cmpSize;
			if (ahv->d.size.endianess == ENDIAN_LITTLE)
				WriteLE(&hdrData->data[pos], ahv->d.size.bytes, val);
			else //if (ahv->d.size.endianess == ENDIAN_BIG)
				WriteBE(&hdrData->data[pos], ahv->d.size.bytes, val);
			pos += ahv->d.size.bytes;
			break;
		}
		case 's':
		{
			size_t copyLen = ahv->d.str.len;
			if (pos + copyLen > hdrData->len)
				copyLen = hdrSpec->len - pos;
			memcpy(&hdrData->data[pos], ahv->d.str.ptr, copyLen);
			pos += ahv->d.str.len;
			break;
		}
		case 'b':
		case 'i':
			hdrData->data[pos] = ahv->d.dataVal;
			pos += 0x01;
			break;
		}
	}
	return pos;
}

static uint8_t ReadArchiveHeader(const FILE_DATA* hdrData, const ARC_HDR_SPEC* hdrSpec, size_t* decSize, size_t* cmpSize)
{
	size_t hdrItem;
	size_t pos;
	if (hdrSpec->len == 0)
		return 0;

	pos = 0x00;
	for (hdrItem = 0; hdrItem < hdrSpec->count; hdrItem++)
	{
		const ARC_HDR_VAL* ahv = &hdrSpec->vals[hdrItem];
		if (pos >= hdrData->len)
			break;	// should never happen
		switch (ahv->type)
		{
		case 'n':
			return 0;
		case 'o':
		case 'c':
		{
			size_t val;
			if (pos + ahv->d.size.bytes > hdrData->len)
				break;
			if (ahv->d.size.endianess == ENDIAN_LITTLE)
				val = ReadLE(&hdrData->data[pos], ahv->d.size.bytes);
			else //if (ahv->d.size.endianess == ENDIAN_BIG)
				val = ReadBE(&hdrData->data[pos], ahv->d.size.bytes);
			if (ahv->d.size.set_val)
			{
				if (ahv->type == 'o')
					*decSize = val;
				else //if (ahv->type == 'c')
					*cmpSize = val;
			}
			pos += ahv->d.size.bytes;
			break;
		}
		case 's':
		{
			size_t checkLen = ahv->d.str.len;
			if (pos + checkLen > hdrData->len)
				checkLen = hdrSpec->len - pos;
			if (memcmp(&hdrData->data[pos], ahv->d.str.ptr, checkLen))
			{
				fprintf(stderr, "Header mismatch at offset 0x%02X: \"%.*s\" != \"%.*s\"\n",
					pos, checkLen, (const char*)&hdrData->data[pos], ahv->d.str.len, ahv->d.str.ptr);
				return 1;
			}
			pos += ahv->d.str.len;
			break;
		}
		case 'b':
			if (hdrData->data[pos] != ahv->d.dataVal)
			{
				fprintf(stderr, "Header mismatch at offset 0x%02X: byte 0x%02X != 0x%02X\n",
					pos, hdrData->data[pos], ahv->d.dataVal);
				return 1;
			}
			pos += 0x01;
			break;
		case 'i':
			pos += 0x01;
			break;
		}
	}
	return 0;
}
