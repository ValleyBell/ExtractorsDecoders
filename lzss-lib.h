#ifndef LZSSLIB_H
#define LZSSLIB_H

#include <stddef.h>

#if !defined(_STDINT_H) && !defined(_STDINT)
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
typedef unsigned char	uint8_t;
#endif	// !HAVE_STDINT_H
#endif	// !_STDINT_H


typedef struct _lzss_config LZSS_CFG;
typedef struct _lzss_compressor LZSS_COMPR;

typedef void (*LZSS_NAMETBL_FUNC)(LZSS_COMPR* lzss, void* user, size_t nameTblSize, uint8_t* nameTblData);

struct _lzss_config
{
	uint8_t flags;			// see LZSS_FLAGS_*
	uint8_t nameTblType;	// see LZSS_NTINIT_*
	uint8_t nameTblValue;	// (for nameTblType == LZSS_NTINIT_VALUE)
	LZSS_NAMETBL_FUNC nameTblFunc;	// (for nameTblType == LZSS_NTINIT_FUNC)
	void* ntFuncParam;		// user parameter for nameTblFunc
	int nameTblStartOfs;	// offset where the name table buffer starts getting written to
	uint8_t eosMode;		// see LZSS_EOSM_*
};

// control word flags
#define LZSS_FLAGS_CTRL_L	0x00	// LSB first (mask 0x01)
#define LZSS_FLAGS_CTRL_M	0x01	// MSB first (mask 0x80)
#define LZSS_FLAGS_CTRLMASK	0x01	// MSB first (mask 0x80)
// match word flags
//	LE: bytes CD AB -> ABCD
//		L_HH	A, BCD -> len = (j & 0xF0)>>4, ofs = ((j & 0x0F)<<8 | i)
//		L_HL	B, ACD -> len = (j & 0x0F)   , ofs = ((j & 0xF0)<<4 | i) -> default
//		L_LH	C, ABD -> len = (i & 0xF0)>>4, ofs = ((i & 0x0F)    | j<<4)
//		L_LL	D, ABC -> len = (i & 0x0F)   , ofs = ((i & 0xF0)>>4 | j<<4)
//	BE: bytes AB CD -> ABCD
//		L_HH	A, BCD -> len = (i & 0xF0)>>4, ofs = ((i & 0x0F)<<8 | j)
//		L_HL	B, ACD -> len = (i & 0x0F)   , ofs = ((i & 0xF0)<<4 | j)
//		L_LH	C, ABD -> len = (j & 0xF0)>>4, ofs = ((j & 0x0F)    | i<<4)
//		L_LL	D, ABC -> len = (j & 0x0F)   , ofs = ((j & 0xF0)>>4 | i<<4)
#define LZSS_FLAGS_MTCH_ELITTLE	0x00	// Little Endian
#define LZSS_FLAGS_MTCH_EBIG	0x40	// Big Endian
#define LZSS_FLAGS_MTCH_EMASK	0x40	// Endianess mask

#define LZSS_FLAGS_MTCH_L_HH	0x00	// length nibble: highest
#define LZSS_FLAGS_MTCH_L_HL	0x10	// length nibble: high byte, low nibble
#define LZSS_FLAGS_MTCH_L_LH	0x20	// length nibble: low byte, low nibble
#define LZSS_FLAGS_MTCH_L_LL	0x30	// length nibble: lowest
#define LZSS_FLAGS_MTCH_LMASK	0x30	// length nibble mask

#define LZSS_FLAGS_MTCH_DEFAULT	(LZSS_FLAGS_MTCH_ELITTLE | LZSS_FLAGS_MTCH_L_HL)

// name table flags
#define LZSS_NTINIT_VALUE	0x00	// initialize name table via constant value
#define LZSS_NTINIT_FUNC	0x01	// initialize name table via function
#define LZSS_NTINIT_NONE	0x02	// don't initialize name table and don't use initial values

// name table start offset, special values
#define LZSS_NTSTOFS_NF		-1	// start at (N-F), i.e. 0xFEE

// end-of-stream mode
#define LZSS_EOSM_NONE		0x00
#define LZSS_EOSM_REF0		0x01	// end with a "null-reference" (offset 0, length 0)


LZSS_COMPR* lzssCreate(const LZSS_CFG* config);
void lzssDestroy(LZSS_COMPR* lzss);
void lzssGetDefaultConfig(LZSS_CFG* config);
const LZSS_CFG* lzssGetConfiguration(const LZSS_COMPR* lzss);
uint8_t lzssEncode(LZSS_COMPR* lzss, size_t bufSize, uint8_t* buffer, size_t* bytesWritten, size_t inSize, const uint8_t* inData);
uint8_t lzssDecode(LZSS_COMPR* lzss, size_t bufSize, uint8_t* buffer, size_t* bytesWritten, size_t inSize, const uint8_t* inData);
void lzssNameTbl_CommonPatterns(LZSS_COMPR* lzss, void* user, size_t nameTblSize, uint8_t* nameTblData);


// error codes
#define LZSS_ERR_OK			0x00	// no error
#define LZSS_ERR_EOF_IN		0x01	// reached early end-of-file while reading input buffer
#define LZSS_ERR_EOF_OUT	0x02	// eached end of output buffer before finishing writing
#define LZSS_ERR_BAD_REF	0x03	// invalid backwards reference beyond start of while uninitialized name table is used


#endif // LZSSLIB_H
