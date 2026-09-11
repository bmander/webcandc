/*
** webcandc: C++ replacements for the VQM32 support routines the VQA32 player
** calls that were assembly (or C bound to VGA hardware) in WINVQ. The
** originals are alongside:
**
**   VB.ASM        WaitNoVB, WaitVB            poll VGA status port 0x3DA
**   TESTVB.CPP    TestVBIBit                  samples port 0x3DA for 1/4 s
**   MCGABUF.ASM   MCGA_BufferCopy, MCGA_Blit  copy into VGA memory at 0xA0000
**   SOSCODEC.ASM  VQA_sosCODECInitStream, VQA_sosCODECDecompressData
**   AUDUNZAP.ASM  AudioUnzap
**
** Video: C&C sets the player up as "buffered, display left to the client"
** (Anim_Init: Vmode 0, VQACFGF_BUFFER), so DrawFrame_Buffer decodes into
** SysMemPage and the game's VQ_Call_Back scales that onto the screen. The
** MCGA draw/page-flip routines are still compiled (VQAMCGA_ON) but never
** selected, and there is no vertical blank bit to test or wait for, so the
** video routines here do nothing.
**
** Audio: rather than port the codecs a second time, these call the Westwood
** sound library's decoders already ported in src/wwlib. SOSCODEC.ASM's
** VQA_sosCODECDecompressData is step for step the general HMI IMA ADPCM
** decoder that OLSOSDEC.ASM has (ported as General_sosCODECDecompressData):
** 8 or 16-bit output, mono or byte-interleaved stereo (left channel from the
** even source bytes, right from the odd), a new token fetched on every even
** sample index, the sample indexes reset on each call and the step carried
** in the structure. Only the structure differs: VQM32's _SOS_COMPRESS_INFO
** has wBitSize/wChannels after the sizes, wwlib's has them at the end, so the
** state is copied across. AudioUnzap (SND1 chunks) goes to Decompress_Frame
** (AUDUNCMP.ASM), the decoder for the same Westwood "ZAP" audio compression;
** C&C's movies carry only SND2 (ADPCM) audio.
*/
#include <vqm32/video.h>
#include <vqm32/compress.h>
#include <vqm32/soscomp.h>		/* VQM32's _SOS_COMPRESS_INFO */

/*
** wwlib's SOSCOMP.H has the same include guard and type names; give its
** declarations their own scope. (The functions keep C linkage.)
*/
#undef _SOS_COMPRESS
namespace wwlib {
#include "../../wwlib/SOSCOMP.H"
}

extern "C" long __cdecl Decompress_Frame(void *source, void *dest, long size);	/* wwlib/AUDUNCMP.CPP */


/*---------------------------------------------------------------------------
 * VIDEO (VB.ASM, TESTVB.CPP, MCGABUF.ASM)
 *-------------------------------------------------------------------------*/

long TestVBIBit(void)
{
	return (0);
}

extern "C" void __cdecl WaitNoVB(short)
{
}

extern "C" void __cdecl WaitVB(short)
{
}

extern "C" void __cdecl MCGA_BufferCopy(unsigned char *, unsigned char *)
{
}

extern "C" void __cdecl MCGA_Blit(unsigned char *, unsigned char *, long, long)
{
}


/*---------------------------------------------------------------------------
 * AUDIO (SOSCODEC.ASM, AUDUNZAP.ASM)
 *-------------------------------------------------------------------------*/

#define SOS_FIELDS(X) \
	X(lpSource) X(lpDest) X(dwCompSize) X(dwUnCompSize) X(wBitSize) X(wChannels) \
	X(dwSampleIndex) X(dwPredicted) X(dwDifference) X(wCodeBuf) X(wCode) X(wStep) X(wIndex) \
	X(dwSampleIndex2) X(dwPredicted2) X(dwDifference2) X(wCodeBuf2) X(wCode2) X(wStep2) X(wIndex2)

/****************************************************************************
* VQA_sosCODECInitStream - Initialize compression stream.
****************************************************************************/
extern "C" void __cdecl VQA_sosCODECInitStream(_SOS_COMPRESS_INFO *info)
{
	info->wIndex = 0;			/* starting index 0 */
	info->wStep = 7;			/* start with a step of 7 */
	info->dwPredicted = 0;		/* no predicted value */
	info->dwSampleIndex = 0;	/* start at head of index */
	info->wIndex2 = 0;
	info->wStep2 = 7;
	info->dwPredicted2 = 0;
	info->dwSampleIndex2 = 0;
}

/****************************************************************************
* VQA_sosCODECDecompressData - Decompress audio data.
*
* Decompress wBytes bytes of output from info->lpSource to info->lpDest.
****************************************************************************/
extern "C" unsigned long __cdecl VQA_sosCODECDecompressData(_SOS_COMPRESS_INFO *info, unsigned long wBytes)
{
	wwlib::_SOS_COMPRESS_INFO ww;

	#define TO_WW(f) ww.f = info->f;
	#define FROM_WW(f) info->f = ww.f;
	SOS_FIELDS(TO_WW)
	wwlib::General_sosCODECDecompressData(&ww, wBytes);
	SOS_FIELDS(FROM_WW)

	return (wBytes);
}

/****************************************************************************
* AudioUnzap - Decompress a Westwood "ZAP" audio frame.
****************************************************************************/
extern "C" long __cdecl AudioUnzap(void *source, void *dest, long size)
{
	return (Decompress_Frame(source, dest, size));
}
