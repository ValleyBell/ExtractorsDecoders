/* LZSS compression and decompression library
   based on LZSS.C by Haruhiko Okumura
   extended with various customizations by Valley Bell
*/
#include <stdlib.h>
#include <string.h>
#include "lzss-lib.h"

struct _lzss_compressor
{
	LZSS_CFG cfg;

	unsigned int N;			/* size of ring buffer, usually 4096 */
	unsigned int F;			/* upper limit for match_length, usually 18 */
	unsigned int THRESHOLD;	/* encode string into position and length
							   if match_length is greater than this, usually 2 */
	#define NIL			N	/* index for root of binary search trees */

	uint8_t* text_buf;		/* ring buffer of size N, with extra F-1 bytes to
							   facilitate string comparison of longest match.
							   These are set by the InsertNode() procedure. */
	int match_position;
	unsigned int match_length;
	int* lson;				/* left & right children & parents -- These constitute binary search trees. */
	int* rson;
	int* dad;
};


LZSS_COMPR* lzssCreate(const LZSS_CFG* config)
{
	LZSS_COMPR* lzss = (LZSS_COMPR*)calloc(1, sizeof(LZSS_COMPR));
	if (lzss == NULL)
		return NULL;

	lzss->cfg = *config;
	lzss->N = 4096;
	lzss->THRESHOLD = 2;
	lzss->F = 0x10 + lzss->THRESHOLD;
	lzss->text_buf = (uint8_t*)malloc(lzss->N + lzss->F - 1);

	lzss->lson = (int*)calloc(lzss->N + 1, sizeof(int));
	lzss->rson = (int*)calloc(lzss->N + 0x101, sizeof(int));
	lzss->dad = (int*)calloc(lzss->N + 1, sizeof(int));
	return lzss;
}

void lzssDestroy(LZSS_COMPR* lzss)
{
	free(lzss->text_buf);
	free(lzss->lson);
	free(lzss->rson);
	free(lzss->dad);
	free(lzss);
}

void lzssGetDefaultConfig(LZSS_CFG* config)
{
	config->flags = LZSS_FLAGS_CTRL_L | LZSS_FLAGS_MTCH_DEFAULT;
	config->nameTblType = LZSS_NTINIT_VALUE;
	config->nameTblValue = ' ';	// space
	config->nameTblFunc = NULL;
	config->ntFuncParam = NULL;
	config->nameTblStartOfs = LZSS_NTSTOFS_NF;
	config->eosMode = LZSS_EOSM_NONE;
	return;
}

const LZSS_CFG* lzssGetConfiguration(const LZSS_COMPR* lzss)
{
	return &lzss->cfg;
}

static void InitTree(LZSS_COMPR* lzss)  /* initialize trees */
{
	unsigned int i;

	/* For i = 0 to N - 1, rson[i] and lson[i] will be the right and
	   left children of node i.  These nodes need not be initialized.
	   Also, dad[i] is the parent of node i.  These are initialized to
	   NIL (= N), which stands for 'not used.'
	   For i = 0 to 255, rson[N + i + 1] is the root of the tree
	   for strings that begin with character i.  These are initialized
	   to NIL.  Note there are 256 trees. */

	for (i = lzss->N + 1; i <= lzss->N + 256; i++) lzss->rson[i] = lzss->NIL;
	for (i = 0; i < lzss->N; i++) lzss->dad[i] = lzss->NIL;
}

static void InsertNode(LZSS_COMPR* lzss, int r)
	/* Inserts string of length F, text_buf[r..r+F-1], into one of the
	   trees (text_buf[r]'th tree) and returns the longest-match position
	   and length via the global variables match_position and match_length.
	   If match_length = F, then removes the old node in favor of the new
	   one, because the old one will be deleted sooner.
	   Note r plays double role, as tree node and position in buffer. */
{
	unsigned int i;
	int  p, cmp;
	uint8_t  *key;

	cmp = 1;  key = &lzss->text_buf[r];  p = lzss->N + 1 + key[0];
	lzss->rson[r] = lzss->lson[r] = lzss->NIL;  lzss->match_length = 0;
	for ( ; ; ) {
		if (cmp >= 0) {
			if (lzss->rson[p] != lzss->NIL) p = lzss->rson[p];
			else {  lzss->rson[p] = r;  lzss->dad[r] = p;  return;  }
		} else {
			if (lzss->lson[p] != lzss->NIL) p = lzss->lson[p];
			else {  lzss->lson[p] = r;  lzss->dad[r] = p;  return;  }
		}
		for (i = 1; i < lzss->F; i++)
			if ((cmp = key[i] - (int)lzss->text_buf[p + i]) != 0)  break;
		if (i > lzss->match_length) {
			lzss->match_position = p;
			if ((lzss->match_length = i) >= lzss->F)  break;
		}
	}
	lzss->dad[r] = lzss->dad[p];  lzss->lson[r] = lzss->lson[p];  lzss->rson[r] = lzss->rson[p];
	lzss->dad[lzss->lson[p]] = r;  lzss->dad[lzss->rson[p]] = r;
	if (lzss->rson[lzss->dad[p]] == p) lzss->rson[lzss->dad[p]] = r;
	else                   lzss->lson[lzss->dad[p]] = r;
	lzss->dad[p] = lzss->NIL;  /* remove p */
}

static void DeleteNode(LZSS_COMPR* lzss, int p)  /* deletes node p from tree */
{
	int  q;

	if (lzss->dad[p] == lzss->NIL) return;  /* not in tree */
	if (lzss->rson[p] == lzss->NIL) q = lzss->lson[p];
	else if (lzss->lson[p] == lzss->NIL) q = lzss->rson[p];
	else {
		q = lzss->lson[p];
		if (lzss->rson[q] != lzss->NIL) {
			do {  q = lzss->rson[q];  } while (lzss->rson[q] != lzss->NIL);
			lzss->rson[lzss->dad[q]] = lzss->lson[q];  lzss->dad[lzss->lson[q]] = lzss->dad[q];
			lzss->lson[q] = lzss->lson[p];  lzss->dad[lzss->lson[p]] = q;
		}
		lzss->rson[q] = lzss->rson[p];  lzss->dad[lzss->rson[p]] = q;
	}
	lzss->dad[q] = lzss->dad[p];
	if (lzss->rson[lzss->dad[p]] == p) lzss->rson[lzss->dad[p]] = q;  else lzss->lson[lzss->dad[p]] = q;
	lzss->dad[p] = lzss->NIL;
}

static void InitNametable(LZSS_COMPR* lzss)
{
	if (lzss->cfg.nameTblType == LZSS_NTINIT_VALUE)
		memset(lzss->text_buf, lzss->cfg.nameTblValue, lzss->N);
	else if (lzss->cfg.nameTblType == LZSS_NTINIT_FUNC)
		lzss->cfg.nameTblFunc(lzss, lzss->cfg.ntFuncParam, lzss->N, lzss->text_buf);
	else
		memset(lzss->text_buf, 0x00, lzss->N);
}

uint8_t lzssEncode(LZSS_COMPR* lzss, size_t bufSize, uint8_t* buffer, size_t* bytesWritten, size_t inSize, const uint8_t* inData)
{
	size_t codesize = 0;		/* code size counter */

	unsigned int i, len, r, s, last_match_length, code_buf_ptr;
	unsigned int maskN = lzss->N - 1;
	uint8_t code_buf[17];	// control byte (1) + 8 reference words (16)
	uint8_t mask;
	size_t inPos;
	size_t outPos;

	if (inSize == 0)  /* text of size zero */
	{
		if (bytesWritten != NULL) *bytesWritten = 0;
		return LZSS_ERR_OK;
	}

	lzss->match_position = 0;
	lzss->match_length = 0;
	InitTree(lzss);  /* initialize trees */
	code_buf[0] = 0;  /* code_buf[1..16] saves eight units of code, and
		code_buf[0] works as eight flags, "1" representing that the unit
		is an unencoded letter (1 byte), "0" a position-and-length pair
		(2 bytes).  Thus, eight units require at most 16 bytes of code. */
	code_buf_ptr = 1;
	mask = ((lzss->cfg.flags & LZSS_FLAGS_CTRLMASK) == LZSS_FLAGS_CTRL_L) ? 0x01 : 0x80;

	InitNametable(lzss);

	inPos = 0;
	outPos = 0;
	if (lzss->cfg.nameTblStartOfs == LZSS_NTSTOFS_NF)
		r = lzss->N - lzss->F;
	else
		r = lzss->cfg.nameTblStartOfs & maskN;
	s = (r + lzss->F) & maskN;
	for (len = 0; len < lzss->F && inPos < inSize; len++)
	{
		uint8_t c = inData[inPos++];
		lzss->text_buf[(r + len) & maskN] = c;  /* Read F bytes into the last F bytes of
			the buffer */
	}
	memcpy(&lzss->text_buf[lzss->N], &lzss->text_buf[0], lzss->F - 1);	// for easy string comparison
	if (lzss->cfg.nameTblType == LZSS_NTINIT_FUNC)
	{
		// build a tree so that the whole nametable is included
		for (i = 1; i <= lzss->N - lzss->F; i++) InsertNode(lzss, (r + lzss->N - i) & maskN);
	}
	else if (lzss->cfg.nameTblType != LZSS_NTINIT_NONE)
	{
		for (i = 1; i <= lzss->F; i++) InsertNode(lzss, (r + lzss->N - i) & maskN);  /* Insert the F strings,
			each of which begins with one or more 'space' characters.  Note
			the order in which these strings are inserted.  This way,
			degenerate trees will be less likely to occur. */
	}
	InsertNode(lzss, r);  /* Finally, insert the whole string just read.  The
		global variables match_length and match_position are set. */
	do {
		if (lzss->match_length > len) lzss->match_length = len;  /* match_length
			may be spuriously long near the end of text. */
		if (lzss->match_length <= lzss->THRESHOLD) {
			lzss->match_length = 1;  /* Not long enough match.  Send one byte. */
			code_buf[0] |= mask;  /* 'send one byte' flag */
			code_buf[code_buf_ptr++] = lzss->text_buf[r];  /* Send uncoded. */
		} else {
			/* Send position and length pair. Note match_length > THRESHOLD. */
			unsigned int mlen = lzss->match_length - (lzss->THRESHOLD + 1);
			uint8_t i, j;
			switch(lzss->cfg.flags & LZSS_FLAGS_MTCH_LMASK)
			{
			case LZSS_FLAGS_MTCH_L_HH:
				i = (uint8_t)lzss->match_position;
				j = (uint8_t)(((lzss->match_position >> 8) & 0x0f) | (mlen << 4));
				break;
			case LZSS_FLAGS_MTCH_L_HL:
			default:
				i = (uint8_t)lzss->match_position;
				j = (uint8_t)(((lzss->match_position >> 4) & 0xf0) | mlen);
				break;
			case LZSS_FLAGS_MTCH_L_LH:
				i = (uint8_t)((lzss->match_position & 0x0f) | (mlen << 4));
				j = (uint8_t)(lzss->match_position >> 4);
				break;
			case LZSS_FLAGS_MTCH_L_LL:
				i = (uint8_t)(((lzss->match_position << 4) & 0xf0) | mlen);
				j = (uint8_t)(lzss->match_position >> 4);
				break;
			}
			if ((lzss->cfg.flags & LZSS_FLAGS_MTCH_EMASK) == LZSS_FLAGS_MTCH_ELITTLE)
			{
				code_buf[code_buf_ptr++] = i;
				code_buf[code_buf_ptr++] = j;
			}
			else //if ((lzss->cfg.flags & LZSS_FLAGS_MTCH_EMASK) == LZSS_FLAGS_MTCH_EBIG)
			{
				code_buf[code_buf_ptr++] = j;
				code_buf[code_buf_ptr++] = i;
			}
		}
		if ((lzss->cfg.flags & LZSS_FLAGS_CTRLMASK) == LZSS_FLAGS_CTRL_L)
			mask <<= 1;	// shift left one bit (low -> high)
		else //if ((lzss->cfg.flags & LZSS_FLAGS_CTRLMASK) == LZSS_FLAGS_CTRL_H)
			mask >>= 1;	// shift right one bit (high -> low)
		if (mask == 0) {
			for (i = 0; i < code_buf_ptr; i++)  /* Send at most 8 units of */
			{
				if (outPos >= bufSize)
				{
					if (bytesWritten != NULL) *bytesWritten = outPos;
					return LZSS_ERR_EOF_OUT;
				}
				buffer[outPos++] = code_buf[i]; /* code together */
			}
			codesize += code_buf_ptr;
			code_buf[0] = 0;  code_buf_ptr = 1;
			mask = ((lzss->cfg.flags & LZSS_FLAGS_CTRLMASK) == LZSS_FLAGS_CTRL_L) ? 0x01 : 0x80;
		}
		last_match_length = lzss->match_length;
		for (i = 0; i < last_match_length; i++) {
			uint8_t c;
			if (inPos >= inSize)
				break;
			DeleteNode(lzss, s);	/* Delete old strings and */
			c = inData[inPos++];
			lzss->text_buf[s] = c;	/* read new bytes */
			if (s < lzss->F - 1) lzss->text_buf[s + lzss->N] = c;  /* If the position is
				near the end of buffer, extend the buffer to make
				string comparison easier. */
			s = (s + 1) & maskN;  r = (r + 1) & maskN;
				/* Since this is a ring buffer, increment the position
				   modulo N. */
			InsertNode(lzss, r);	/* Register the string in text_buf[r..r+F-1] */
		}
		while (i++ < last_match_length) {	/* After the end of text, */
			DeleteNode(lzss, s);			/* no need to read, but */
			s = (s + 1) & maskN;  r = (r + 1) & maskN;
			if (--len) InsertNode(lzss, r);	/* buffer may not be empty. */
		}
	} while (len > 0);	/* until length of string to be processed is zero */
	if (code_buf_ptr > 1) {		/* Send remaining code. */
		for (i = 0; i < code_buf_ptr; i++)
		{
			if (outPos >= bufSize)
			{
				if (bytesWritten != NULL) *bytesWritten = outPos;
				return LZSS_ERR_EOF_OUT;
			}
			buffer[outPos++] = code_buf[i];
		}
		codesize += code_buf_ptr;
	}
	if (lzss->cfg.eosMode == LZSS_EOSM_REF0)
	{
		// add null-reference
		if (code_buf_ptr > 1)
		{
			i = 1;		// just write the terminating reference word
		}
		else
		{
			code_buf[0] = 0;	// "reference flag" control byte
			i = 0;	// write control byte + reference word
		}
		code_buf[1] = code_buf[2] = 0;	// reference word
		code_buf_ptr = 3;
		for (; i < code_buf_ptr; i++)
		{
			if (outPos >= bufSize)
			{
				if (bytesWritten != NULL) *bytesWritten = outPos;
				return LZSS_ERR_EOF_OUT;
			}
			buffer[outPos++] = code_buf[i];
		}
	}

	if (bytesWritten != NULL) *bytesWritten = outPos;
	return LZSS_ERR_OK;
}

uint8_t lzssDecode(LZSS_COMPR* lzss, size_t bufSize, uint8_t* buffer, size_t* bytesWritten, size_t inSize, const uint8_t* inData)
{
	unsigned int maskN = lzss->N - 1;
	unsigned int r;	// ring buffer position
	uint8_t flags;
	unsigned int flag_bits;
	size_t inPos;
	size_t outPos;

	InitNametable(lzss);

	if (lzss->cfg.nameTblStartOfs == LZSS_NTSTOFS_NF)
		r = lzss->N - lzss->F;
	else
		r = lzss->cfg.nameTblStartOfs & maskN;
	flags = 0;
	flag_bits = 0;
	inPos = 0;
	outPos = 0;
	while(1)
	{
		unsigned int lz_flag;

		if (flag_bits == 0)
		{
			if (inPos >= inSize)
				break;	// EOF is valid here
			flags = inData[inPos++];
			flag_bits = 8;
		}
		if ((lzss->cfg.flags & LZSS_FLAGS_CTRLMASK) == LZSS_FLAGS_CTRL_L)	// check lowest bit / shift right
		{
			lz_flag = flags & 1;
			flags >>= 1;
			flag_bits--;
		}
		else //if ((lzss->cfg.flags & LZSS_FLAGS_CTRLMASK) == LZSS_FLAGS_CTRL_H)	// check highest bit / shift left
		{
			lz_flag = flags & 0x80;
			flags <<= 1;
			flag_bits--;
		}

		if (lz_flag) {
			uint8_t c;
			if (inPos >= inSize)
			{
				if (bytesWritten != NULL) *bytesWritten = outPos;
				return LZSS_ERR_EOF_IN;
			}
			if (outPos >= bufSize)
			{
				if (bytesWritten != NULL) *bytesWritten = outPos;
				return LZSS_ERR_EOF_OUT;
			}
			c = inData[inPos++];
			buffer[outPos++] = c;
			lzss->text_buf[r++] = c;
			r &= maskN;
		} else {
			uint8_t i, j;
			unsigned int k, len, ofs;

			if (inPos == inSize)
				break;	// EOF in this way is valid here
			if (inPos+1 >= inSize)
			{
				if (bytesWritten != NULL) *bytesWritten = outPos;
				return LZSS_ERR_EOF_IN;
			}
			if ((lzss->cfg.flags & LZSS_FLAGS_MTCH_EMASK) == LZSS_FLAGS_MTCH_ELITTLE)
			{
				i = inData[inPos+0];
				j = inData[inPos+1];
			}
			else //if ((lzss->cfg.flags & LZSS_FLAGS_MTCH_EMASK) == LZSS_FLAGS_MTCH_EBIG)
			{
				j = inData[inPos+0];
				i = inData[inPos+1];
			}
			if (lzss->cfg.eosMode == LZSS_EOSM_REF0)
			{
				if (i == 0 && j == 0)
					break;	// null-reference ends the stream
			}
			inPos += 2;
			switch(lzss->cfg.flags & LZSS_FLAGS_MTCH_LMASK)
			{
			case LZSS_FLAGS_MTCH_L_HH:
				ofs = ((j & 0x0f) << 8) | i;
				len = ((j & 0xf0) >> 4);
				break;
			case LZSS_FLAGS_MTCH_L_HL:
			default:
				ofs = ((j & 0xf0) << 4) | i;
				len = (j & 0x0f);
				break;
			case LZSS_FLAGS_MTCH_L_LH:
				ofs = (i & 0x0f) | (j << 4);
				len = ((i & 0xf0) >> 4);
				break;
			case LZSS_FLAGS_MTCH_L_LL:
				ofs = ((i & 0xf0) >> 4) | (j << 4);
				len = (i & 0x0f);
				break;
			}
			len += lzss->THRESHOLD + 1;
			if (lzss->cfg.nameTblType == LZSS_NTINIT_NONE)
			{
				unsigned int ofs_back = (r + lzss->N - ofs) & maskN;
				if (ofs_back > outPos)	// make sure we don't reference data beyond the start of the file
				{
					if (bytesWritten != NULL) *bytesWritten = outPos;
					return LZSS_ERR_BAD_REF;
				}
			}

			for (k = 0; k < len; k++)
			{
				unsigned int offset = (ofs + k) & maskN;
				uint8_t c = lzss->text_buf[offset];
				if (outPos >= bufSize)
				{
					if (bytesWritten != NULL) *bytesWritten = outPos;
					return LZSS_ERR_EOF_OUT;
				}
				buffer[outPos++] = c;
				lzss->text_buf[r++] = c;
				r &= maskN;
			}
		}
	}

	if (bytesWritten != NULL) *bytesWritten = outPos;
	return LZSS_ERR_OK;
}

void lzssNameTbl_CommonPatterns(LZSS_COMPR* lzss, void* user, size_t nameTblSize, uint8_t* nameTblData)
{
	// Important Note: These are non-standard values and ARE used by the compressed data.
	unsigned int bufPos;
	unsigned int regD0;
	unsigned int regD1;

	// LZSS table initialization, originally from Arcus Odyssey X68000, M_DRV.X
	// verified using TSTAR.EXE
	bufPos = 0x0000;
	// 000..CFF (0x0D bytes of 00, 01, 02, ... FF each)
	for (regD0 = 0x00; regD0 < 0x100; regD0 ++)
	{
		for (regD1 = 0x00; regD1 < 0x0D; regD1 ++, bufPos ++)
			nameTblData[bufPos] = (uint8_t)regD0;
	}
	// AD00..ADFF (00 .. FF)
	for (regD0 = 0x00; regD0 < 0x100; regD0 ++, bufPos ++)
		nameTblData[bufPos] = (uint8_t)regD0;
	// AE00..AEFF (FF .. 00)
	do
	{
		regD0 --;
		nameTblData[bufPos] = (uint8_t)regD0;
		bufPos ++;
	} while(regD0 > 0x00);
	// AF00..AF7F (0x80 times 00)
	for (regD0 = 0x00; regD0 < 0x80; regD0 ++, bufPos ++)
		nameTblData[bufPos] = 0x00;
	// AF80..AFED (0x6E times 20/space)
	//for (regD0 = 0x00; regD0 < 0x80 - lzss->F; regD0 ++, bufPos ++)
	for (regD0 = 0x00; regD0 < 0x80; regD0 ++, bufPos ++)	// let's just be safe and fill everything
		nameTblData[bufPos] = ' ';

	return;
}
