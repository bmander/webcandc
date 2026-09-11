/*
** webcandc: C++ port of the HMI SOS ADPCM (IMA 4:1) decoders used by the
** Westwood sound library (SCOMP_SOS samples -- every .AUD in the game):
**
**   SOSCODEC.ASM  sosCODECInitStream, sosCODECDecompressData
**                 (Jonathan Lanier's 8/95 table-driven rewrite; 16-bit mono
**                 only -- other formats return without decoding)
**   OLSOSDEC.ASM  General_sosCODECDecompressData (the older general decoder:
**                 8/16-bit, mono/stereo), used by Sample_Copy for the rest.
**
** The originals are alongside. The table-driven decoder's DIFFTB.INC /
** INDEXTB.INC tables are exactly the IMA step arithmetic below: difference =
** step>>3 (+step if code&4, +step>>1 if code&2, +step>>2 if code&1), negated
** if code&8; next index = clamp(index + wCODECIndexTab[code], 0, 88). Low
** nybble first; samples clamp to -32768..32767. The DPMI lock/unlock entry
** points are not needed.
*/
#include <windows.h>
#include "soscomp.h"

static short const wCODECIndexTab[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static short const wCODECStepTab[89] = {
	7,     8,     9,     10,    11,    12,     13,    14,    16,    17,
	19,    21,    23,    25,    28,    31,     34,    37,    41,    45,
	50,    55,    60,    66,    73,    80,     88,    97,    107,   118,
	130,   143,   157,   173,   190,   209,    230,   253,   279,   307,
	337,   371,   408,   449,   494,   544,    598,   658,   724,   796,
	876,   963,   1060,  1166,  1282,  1411,   1552,  1707,  1878,  2066,
	2272,  2499,  2749,  3024,  3327,  3660,   4026,  4428,  4871,  5358,
	5894,  6484,  7132,  7845,  8630,  9493,   10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623,  27086, 29794, 32767
};

static inline long Difference(int step, int code)
{
	long diff = step >> 3;
	if (code & 4) diff += step;
	if (code & 2) diff += step >> 1;
	if (code & 1) diff += step >> 2;
	return (code & 8) ? -diff : diff;
}

static inline long Clamp_Sample(long sample)
{
	if (sample > 32767) return 32767;
	if (sample < -32768) return -32768;
	return sample;
}

static inline int Next_Index(int index, int code)
{
	index += wCODECIndexTab[code];
	if (index < 0) return 0;
	if (index > 88) return 88;
	return index;
}

/****************************************************************************
* sosCODECInitStream - Initialize compression stream.
*
* SOSCODEC.ASM resets the index and predicted value of both channels;
* OLSOSDEC.ASM (linked into the same library) also starts the step at 7 and
* the sample index at 0. Do both, so General_sosCODECDecompressData (which
* carries wStep between calls) starts from a defined state.
****************************************************************************/
extern "C" void __cdecl sosCODECInitStream(_SOS_COMPRESS_INFO *info)
{
	info->wIndex = 0;
	info->wStep = 7;
	info->dwPredicted = 0;
	info->dwSampleIndex = 0;
	info->wIndex2 = 0;
	info->wStep2 = 7;
	info->dwPredicted2 = 0;
	info->dwSampleIndex2 = 0;
}

/****************************************************************************
* sosCODECDecompressData - Decompress audio data (SOSCODEC.ASM).
*
* Decompress 16-bit mono data from a 4:1 ADPCM stream: wBytes bytes of
* output from lpSource into lpDest. Only the predicted value and index are
* carried in the structure. Returns wBytes.
****************************************************************************/
extern "C" unsigned long __cdecl sosCODECDecompressData(_SOS_COMPRESS_INFO *info, unsigned long wBytes)
{
	/*
	** "for now only ADPCM 4:1 Mono 16-bit is implemented"; the assembly
	** returned without touching anything for the other formats.
	*/
	if (info->wBitSize != 16 || info->wChannels != 1) return(0);

	unsigned char const *src = (unsigned char const *)info->lpSource;
	unsigned char *dst = (unsigned char *)info->lpDest;
	long sample = info->dwPredicted;
	int index = info->wIndex;
	unsigned long samples = wBytes >> 1;

	for (unsigned long i = 0; i < samples; i++) {
		int code = (i & 1) ? (src[i >> 1] >> 4) : (src[i >> 1] & 0x0F);
		sample = Clamp_Sample(sample + Difference(wCODECStepTab[index], code));
		dst[0] = (unsigned char)sample;
		dst[1] = (unsigned char)(sample >> 8);
		dst += 2;
		index = Next_Index(index, code);
	}

	info->dwPredicted = sample;
	info->wIndex = (short)index;
	return(wBytes);
}

/*
** One channel of General_sosCODECDecompressData. The channel's state lives in
** the structure (the assembly updates it in place); tokens are fetched every
** other sample, and 8-bit output is the high byte of the sample, flipped to
** unsigned.
*/
static void General_Channel(unsigned char const *src, int src_step, unsigned char *dst, int dst_step,
	int bits, long count, long count_step, unsigned long &sample_index, short &code_buf, short &code_out,
	long &difference, short &step, long &predicted, short &index)
{
	while (count > 0) {
		int code;
		if (sample_index & 1) {
			code = (code_buf >> 4) & 0x0F;
		} else {
			code_buf = *src;
			src += src_step;
			code = code_buf & 0x0F;
		}
		code_out = (short)code;

		difference = Difference((unsigned short)step, code);
		predicted = Clamp_Sample(predicted + difference);

		if (bits == 16) {
			dst[0] = (unsigned char)predicted;
			dst[1] = (unsigned char)(predicted >> 8);
		} else {
			dst[0] = (unsigned char)((predicted >> 8) ^ 0x80);
		}
		dst += dst_step;

		index = (short)Next_Index(index, code);
		step = wCODECStepTab[index];
		sample_index++;
		count -= count_step;
	}
}

/****************************************************************************
* General_sosCODECDecompressData - Decompress audio data (OLSOSDEC.ASM).
*
* Decompress wBytes bytes of 8 or 16-bit, mono or stereo output. Stereo
* sources interleave one byte (two samples) per channel. Returns wBytes.
****************************************************************************/
extern "C" unsigned long __cdecl General_sosCODECDecompressData(_SOS_COMPRESS_INFO *info, unsigned long wBytes)
{
	long samples = (info->wBitSize == 16) ? (long)(wBytes >> 1) : (long)wBytes;
	int size = (info->wBitSize == 16) ? 2 : 1;
	unsigned char const *src = (unsigned char const *)info->lpSource;
	unsigned char *dst = (unsigned char *)info->lpDest;

	info->dwSampleIndex = 0;
	info->dwSampleIndex2 = 0;

	/*
	** (The assembly's count-down loops test for zero after the first sample,
	** so a zero-byte request would run away; nothing asks for one.)
	*/
	if (samples <= 0) return(wBytes);

	if (info->wChannels != 2) {
		General_Channel(src, 1, dst, size, info->wBitSize, samples, 1,
			info->dwSampleIndex, info->wCodeBuf, info->wCode, info->dwDifference,
			info->wStep, info->dwPredicted, info->wIndex);
	} else {
		General_Channel(src, 2, dst, size * 2, info->wBitSize, samples, 2,
			info->dwSampleIndex, info->wCodeBuf, info->wCode, info->dwDifference,
			info->wStep, info->dwPredicted, info->wIndex);
		General_Channel(src + 1, 2, dst + size, size * 2, info->wBitSize, samples, 2,
			info->dwSampleIndex2, info->wCodeBuf2, info->wCode2, info->dwDifference2,
			info->wStep2, info->dwPredicted2, info->wIndex2);
	}
	return(wBytes);
}
