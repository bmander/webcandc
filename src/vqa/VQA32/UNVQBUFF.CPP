/*
** webcandc: C++ port of UnVQ_4x2 from UNVQBUFF.ASM (the original is
** alongside). 4x2 is the only block size C&C's movies use and the only UnVQ
** routine VQAPLAY.H enables (VQABLOCK_4X2); the 2x2, 2x3, 4x4 and "woofer"
** variants in the same file are not built.
**
** The vector pointers are two planes of `blocksperrow * numrows` bytes: the
** low bytes of every block, then the high bytes. A high byte of 0x0F marks a
** one-color block whose color is the low byte; otherwise (high << 8 | low)
** indexes an 8-byte codeword (two rows of four pixels) in the codebook.
*/
#include <string.h>
#include "unvq.h"

extern "C" void __cdecl UnVQ_4x2(unsigned char *codebook, unsigned char *pointers,
		unsigned char *buffer, unsigned long blocksperrow,
		unsigned long numrows, unsigned long bufwidth)
{
	unsigned long entries = numrows * blocksperrow;	/* Offset of the high-byte plane */
	unsigned char *data_end = pointers + entries;	/* End of the low-byte plane */
	unsigned char *rowstart = buffer;				/* "edi_startval" */

	while (pointers < data_end) {
		unsigned char *dst = rowstart;

		for (unsigned long n = blocksperrow; n != 0; n--) {
			unsigned long lo = pointers[0];
			unsigned long hi = pointers[entries];
			pointers++;

			if (hi == 0x0F) {

				/* Draw 1-color block */
				memset(dst, (int)lo, 4);
				memset(dst + bufwidth, (int)lo, 4);
			} else {

				/* Draw multi-color block */
				unsigned char const *codeword = codebook + (((hi << 8) | lo) << 3);
				memcpy(dst, codeword, 4);
				memcpy(dst + bufwidth, codeword + 4, 4);
			}

			dst += 4;
		}

		/* Advance to the next destination row of blocks. */
		rowstart += (bufwidth << 1);
	}
}
