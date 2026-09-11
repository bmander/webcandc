/*
** webcandc: portable C++ replacements for the assembly routines declared in
** IFF.H (Westwood 32-bit library):
**
**   LCWCOMP.ASM  -- LCW_Compress
**   PACK2PLN.ASM -- Pack_2_Plane
**
** (LCW_Uncompress, the third IFF.H routine, lives in DrawMisc.cpp.)
*/

#include "IFF.H"

namespace {

enum {
	LCW_HASH_BITS = 15,
	LCW_HASH_SIZE = 1 << LCW_HASH_BITS,
	LCW_MAX_CHAIN = 128,		// match candidates examined per position
	LCW_MAX_COUNT = 0xFFFF		// largest count a 16-bit length field holds
};

inline unsigned LCW_Hash(unsigned char const *p)
{
	return (((unsigned)p[0] << 10) ^ ((unsigned)p[1] << 5) ^ (unsigned)p[2]) & (LCW_HASH_SIZE - 1);
}

}	// namespace


/***************************************************************************
 * LCW_Compress -- Compresses data with the LCW ("format80") method.       *
 *                                                                         *
 * Output command codes (decoded by LCW_Uncompress):                       *
 *   0xxxyyyy,yyyyyyyy  copy x+3 bytes from dest, y bytes back (relative)  *
 *   10xxxxxx,n1..nx    copy the next x (1..63) bytes from source          *
 *   11xxxxxx,w1        copy x+3 (3..64) bytes from dest offset w1         *
 *   11111110,w1,b1     run of byte b1 for w1 bytes                        *
 *   11111111,w1,w2     copy w1 bytes from dest offset w2                  *
 *   10000000           end of data                                        *
 *                                                                         *
 * INPUT:   source - data to compress                                      *
 *          dest   - output buffer; worst case needs                       *
 *                   length + length/63 + 2 bytes                          *
 *          length - number of bytes to compress                           *
 *                                                                         *
 * OUTPUT:  Number of bytes written to dest.                               *
 *                                                                         *
 * WARNINGS: Copies never overlap the bytes they produce, so any decoder   *
 *           (byte-wise or memcpy based) reproduces the data. Absolute     *
 *           offsets are 16-bit, so beyond 64K only relative copies and    *
 *           runs are used.                                                *
 *=========================================================================*/
extern "C" unsigned long __cdecl LCW_Compress(void *source, void *dest, unsigned long length)
{
	unsigned char const *const src = (unsigned char const *)source;
	unsigned char *out = (unsigned char *)dest;
	unsigned char *const out_start = out;
	unsigned long const n = length;

	/*
	** Hash chains of 3-byte prefixes for finding matches. If the tables can't
	** be allocated the compressor still works, using only literals and runs.
	*/
	long *head = NULL;
	long *prev = NULL;
	if (n >= 3) {
		head = (long *)malloc(LCW_HASH_SIZE * sizeof(long));
		prev = (long *)malloc(n * sizeof(long));
		if (!head || !prev) {
			free(head);
			free(prev);
			head = prev = NULL;
		} else {
			for (int i = 0; i < LCW_HASH_SIZE; i++) head[i] = -1;
		}
	}

	unsigned char *literal_op = NULL;		// open "copy from source" command
	unsigned long pos = 0;

	while (pos < n) {
		unsigned long const remain = n - pos;
		unsigned long const max_len = (remain < LCW_MAX_COUNT) ? remain : LCW_MAX_COUNT;

		/*
		** Length of the run of identical bytes starting here.
		*/
		unsigned long run = 1;
		while (run < max_len && src[pos + run] == src[pos]) run++;

		/*
		** Longest earlier match that some copy command can encode.
		*/
		unsigned long best_len = 0;
		unsigned long best_pos = 0;
		if (head && remain >= 3) {
			int chain = LCW_MAX_CHAIN;
			for (long cand = head[LCW_Hash(src + pos)]; cand >= 0 && chain-- > 0; cand = prev[cand]) {
				unsigned long const c = (unsigned long)cand;
				unsigned long const rel = pos - c;
				bool const rel_ok = rel <= 0xFFF;
				bool const abs_ok = c <= 0xFFFF;
				if (!rel_ok && !abs_ok) continue;

				unsigned long limit = max_len;
				if (limit > rel) limit = rel;			// no self-overlapping copies
				if (!abs_ok && limit > 10) limit = 10;	// relative copies max out at 10

				unsigned long len = 0;
				while (len < limit && src[c + len] == src[pos + len]) len++;
				if (len > best_len) {
					best_len = len;
					best_pos = c;
					if (len == max_len) break;
				}
			}
		}

		unsigned long advance;
		if (run >= 5 && run >= best_len) {
			*out++ = 0xFE;
			*out++ = (unsigned char)run;
			*out++ = (unsigned char)(run >> 8);
			*out++ = src[pos];
			advance = run;
			literal_op = NULL;
		} else if (best_len >= 3) {
			unsigned long const rel = pos - best_pos;
			if (best_len <= 10 && rel <= 0xFFF) {
				*out++ = (unsigned char)(((best_len - 3) << 4) | (rel >> 8));
				*out++ = (unsigned char)rel;
			} else if (best_len <= 64) {
				*out++ = (unsigned char)(0xC0 | (best_len - 3));
				*out++ = (unsigned char)best_pos;
				*out++ = (unsigned char)(best_pos >> 8);
			} else {
				*out++ = 0xFF;
				*out++ = (unsigned char)best_len;
				*out++ = (unsigned char)(best_len >> 8);
				*out++ = (unsigned char)best_pos;
				*out++ = (unsigned char)(best_pos >> 8);
			}
			advance = best_len;
			literal_op = NULL;
		} else {
			if (literal_op && (*literal_op & 0x3F) < 0x3F) {
				(*literal_op)++;
			} else {
				literal_op = out;
				*out++ = 0x81;
			}
			*out++ = src[pos];
			advance = 1;
		}

		/*
		** Enter the consumed positions into the hash chains.
		*/
		if (head) {
			for (unsigned long p = pos; p < pos + advance; p++) {
				if (p + 3 <= n) {
					unsigned const h = LCW_Hash(src + p);
					prev[p] = head[h];
					head[h] = (long)p;
				}
			}
		}
		pos += advance;
	}

	*out++ = 0x80;

	free(head);
	free(prev);
	return (unsigned long)(out - out_start);
}


/***************************************************************************
 * Pack_2_Plane -- Converts one 320 pixel row to one bitplane.             *
 *                                                                         *
 * INPUT:   buffer   - 40 byte output buffer for the plane row             *
 *          pageptr  - 320 bytes of chunky (one byte per pixel) data       *
 *          planebit - the bit of each pixel to extract (low byte used)    *
 *                                                                         *
 * OUTPUT:  Each output bit (MSB first) is set if the pixel has planebit.  *
 *=========================================================================*/
extern "C" void __cdecl Pack_2_Plane(void *buffer, void * pageptr, int planebit)
{
	unsigned char *dst = (unsigned char *)buffer;
	unsigned char const *src = (unsigned char const *)pageptr;
	unsigned char const mask = (unsigned char)planebit;

	for (int column = 0; column < 320; column += 8) {
		unsigned char bits = 0;
		for (int i = 0; i < 8; i++) {
			if (src[column + i] & mask) bits |= (unsigned char)(0x80 >> i);
		}
		*dst++ = bits;
	}
}
