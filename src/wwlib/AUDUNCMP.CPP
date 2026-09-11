/*
** webcandc: C++ port of AUDUNCMP.ASM (Westwood 32 bit Audio Library, PWG
** 3/14/1995) -- Decompress_Frame, the decoder for SCOMP_WESTWOOD sound frames
** ("Audio_Unzap"), as called by Sample_Copy in SOUNDINT.CPP. It follows the
** assembly instruction for instruction; the original is alongside in
** AUDUNCMP.ASM. The DPMI lock/unlock entry points are not needed (nothing is
** paged out from under an interrupt here).
**
** Each code byte holds a 2-bit code and a 6-bit argument:
**   0  (count+1) bytes of four 2-bit deltas
**   1  (count+1) bytes of two 4-bit deltas
**   2  bit 5 set: one signed 5-bit delta; clear: (count+1) raw samples follow
**   3  the previous sample repeated (count+1) times
** Delta results saturate to 0..255, except the 5-bit delta, which the
** assembly adds with plain 8-bit wraparound.
*/
#include <windows.h>
#include "dsound.h"
#include "soundint.h"

#define CODE_2BIT		0
#define CODE_4BIT		1
#define CODE_RAW		2
#define CODE_SILENCE	3

static signed char const _2bitdecode[4] = {-2, -1, 0, 1};
static signed char const _4bitdecode[16] = {-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8};

/*
** "add dl,delta" followed by the carry test: clamp the sum to 0..255.
*/
static inline unsigned char Add_Delta(unsigned char sample, signed char delta)
{
	int sum = sample + delta;
	if (sum > 255) return 255;
	if (sum < 0) return 0;
	return (unsigned char)sum;
}

/***************************************************************************
 * DECOMPRESS_FRAME -- Uncompresses a WW compressed audio frame            *
 *                                                                         *
 * INPUT:	void * source - pointer to encoded audio data                  *
 *		void * dest   - pointer to decompression area                      *
 *		long size     - the maximum size of destination buffer             *
 *                                                                         *
 * OUTPUT:      long - the number of bytes read from the source            *
 *                                                                         *
 * HISTORY:                                                                *
 *   03/14/1995 PWG : Created.                                             *
 *=========================================================================*/
extern "C" long __cdecl Decompress_Frame(void * source, void * dest, long size)
{
	long incount = 0;			// Bytes read from source.

	if (!source || !dest || !size) return(incount);

	unsigned char const * src = (unsigned char const *)source;
	unsigned char * dst = (unsigned char *)dest;
	long count = size;			// Bytes left to fill in the dest buffer (ecx).
	unsigned char previous = 0x80;	// Previous sample (dl).

	while (count > 0) {
		unsigned code = *src++;
		incount++;
		unsigned arg = code & 0x3F;
		code >>= 6;

		if (code == CODE_RAW) {
			if (arg & 0x20) {
				/*
				** The lower 5 bits are a signed delta: "shl al,3 / sar al,3 / add dl,al".
				*/
				previous = (unsigned char)(previous + ((signed char)(arg << 3) >> 3));
				*dst++ = previous;
				count--;
			} else {
				/*
				** A run of (arg+1) raw samples.
				*/
				long run = arg + 1;
				memcpy(dst, src, run);
				src += run;
				dst += run;
				incount += run;
				count -= run;
				previous = dst[-1];
			}
			continue;
		}

		int bytes = arg + 1;	// "inc al": the following codes use AL+1.
		switch (code) {
			case CODE_4BIT:
				do {
					unsigned char deltas = *src++;
					incount++;
					previous = Add_Delta(previous, _4bitdecode[deltas & 0x0F]);
					*dst++ = previous;
					previous = Add_Delta(previous, _4bitdecode[deltas >> 4]);
					*dst++ = previous;
					count -= 2;
				} while (--bytes);
				break;

			case CODE_2BIT:
				do {
					unsigned char deltas = *src++;
					incount++;
					for (int shift = 0; shift < 8; shift += 2) {
						previous = Add_Delta(previous, _2bitdecode[(deltas >> shift) & 0x03]);
						*dst++ = previous;
					}
					count -= 4;
				} while (--bytes);
				break;

			default:	// CODE_SILENCE: zero deltas repeat the previous sample.
				memset(dst, previous, bytes);
				dst += bytes;
				count -= bytes;
				break;
		}
	}
	return(incount);
}
