/*
**	Command & Conquer(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
**	webcandc: KEYFBUFF.CPP -- C++ port of KEYFBUFF.ASM (Buffer_Frame_To_Page).
**
**	Draws a linear (uncompressed) shape frame into a viewport, with clipping and the
**	transparent / ghost / fading / predator effects. Two drawing paths, as in the asm:
**
**	- "old" path: whole-rectangle blitters (BF_*), used when the big shape buffer is off,
**	  when the shape needs clipping, or when it was set up for different flags;
**	- "new" path: per-line routines (Single_Line_*) picked by the line header bytes that
**	  Setup_Shape_Header builds in the big shape buffer on the first draw.
**
**	References: reference/Vanilla-Conquer/common/keybuff.cpp (portable port of the same
**	routine). The asm is the spec and differs from Vanilla in several places, all following
**	the asm here: left/top clip edges, the negative predator table (-1,-3,-2,-5,-2,-4,-3,-1)
**	and how a negative predator frame is folded, no PredatorLimit clamp, headers are not
**	rebuilt when the flags change (old path used instead), and BF_Predator_Fading advances
**	rows correctly.
**
**	Optional arguments, in this order (only those whose flag is set):
**		SHAPE_GHOST		unsigned char *ghost      (256 byte is-translucent table,
**		                                           followed by the 256x256 translucency table)
**		SHAPE_FADING	unsigned char *fade, int count   (count is masked to 0..63)
**		SHAPE_PREDATOR	int frame                  (sign selects the table below/right)
**		SHAPE_PARTIAL	int partial                (0..255 predator density)
*/

#include	"function.h"


/*
**	Shape flags as KEYFBUFF.ASM defines them.
*/
#define	KF_SHAPE_CENTER			0x0020
#define	KF_SHAPE_TRANS			0x0040
#define	KF_SHAPE_FADING			0x0100
#define	KF_SHAPE_PREDATOR		0x0200
#define	KF_SHAPE_GHOST			0x1000
#define	KF_SHAPE_PARTIAL		0x4000
#define	KF_HEADER_FLAGS			(KF_SHAPE_TRANS | KF_SHAPE_FADING | KF_SHAPE_PREDATOR | KF_SHAPE_GHOST)

/*
**	Old path routine selector bits (BufferFrameTable index).
*/
#define	FLAG_TRANS				1
#define	FLAG_GHOST				2
#define	FLAG_FADING				4
#define	FLAG_PREDATOR			8

/*
**	Line header bits (new path).
*/
#define	BLIT_TRANSPARENT		1
#define	BLIT_GHOST				2
#define	BLIT_FADING				4
#define	BLIT_PREDATOR			8
#define	BLIT_SKIP				16
#define	BLIT_ALL				(BLIT_TRANSPARENT | BLIT_GHOST | BLIT_FADING | BLIT_PREDATOR | BLIT_SKIP)

#define	PRED_MASK				0x0E


/*
**	Big shape buffer state, owned by KEYFRAME.CPP.
*/
extern "C" {
	extern char *	BigShapeBufferStart;
	extern char *	TheaterShapeBufferStart;
	extern BOOL		UseBigShapeBuffer;
}

/*
**	Same layout as ShapeHeaderType in KEYFRAME.CPP; one header byte per line follows it.
*/
typedef struct {
	unsigned	draw_flags;			// -1 until the line headers have been built.
	char *	shape_data;			// Offset of the raw frame from the buffer start.
	int		shape_buffer;		// 1 if the shape is in the theater buffer.
} KFShapeHeaderType;


/*
**	Predator effect state (KEYFBUFF.ASM DATASEG). PredOffset mirrors the low byte of
**	BFPredOffset (a byte offset 0,2,..14 into a word table); PredNegative mirrors its
**	0xFFFFFF00 high part, which selects the table of "one row down" offsets.
*/
static short const		BFPredTable[8] = {1, 3, 2, 5, 2, 3, 4, 1};
static short const		BFPredNegTable[8] = {-1, -3, -2, -5, -2, -4, -3, -1};
static unsigned short	BFPredNegOffsets[8];
static unsigned			PredOffset = 0;
static bool				PredNegative = false;
static unsigned			BFPartialPred = 0;
static unsigned			BFPartialCount = 0;


/*
**	Per call drawing parameters (locals of the asm routine).
*/
struct BFContext {
	unsigned char const *	IsTranslucent;
	unsigned char const *	Translucent;
	unsigned char const *	FadingTable;
	int						FadingNum;
};


/*
**	Advance the partial predator counter; true if this pixel is a predator pixel.
**	("add ebx,[BFPartialPred] / or bh,bh / jnz get_pred / ... xor bh,bh")
*/
static inline bool Predator_Step(void)
{
	unsigned sum = BFPartialCount + BFPartialPred;

	if (sum & 0xFF00U) {
		BFPartialCount = sum & ~0xFF00U;
		return(true);
	}
	BFPartialCount = sum;
	return(false);
}


/*
**	Fetch the current predator pixel offset and step the table index. The asm reads the
**	word with "movzx", so the offset is always an unsigned 16 bit value.
*/
static inline unsigned Predator_Offset(void)
{
	unsigned offset = PredNegative ? BFPredNegOffsets[PredOffset >> 1] : (unsigned short)BFPredTable[PredOffset >> 1];

	PredOffset = (PredOffset + 2) & PRED_MASK;
	return(offset);
}


/*
**	Single_Line_Predator_Fading(_Trans) step the index with "and [BYTE BFPredOffset],2"
**	instead of "add"; that asm quirk is kept for those two routines.
*/
static inline unsigned Predator_Offset_And_Quirk(void)
{
	unsigned offset = PredNegative ? BFPredNegOffsets[PredOffset >> 1] : (unsigned short)BFPredTable[PredOffset >> 1];

	PredOffset = (PredOffset & 2) & PRED_MASK;
	return(offset);
}


static inline unsigned char Ghost(unsigned char pixel, unsigned char background, BFContext const & ctx)
{
	unsigned char lucent = ctx.IsTranslucent[pixel];

	if (lucent != 0xFF) {
		pixel = ctx.Translucent[(lucent << 8) + background];
	}
	return(pixel);
}


/*
**	webcandc: the asm fade loop is "dec ecx / jnz", so a count of 0 would loop 2^32 times.
**	CC_Draw_Shape always passes 1; 0 is treated as "no remap" here.
*/
static inline unsigned char Fade(unsigned char pixel, BFContext const & ctx)
{
	for (int count = ctx.FadingNum; count > 0; count--) {
		pixel = ctx.FadingTable[pixel];
	}
	return(pixel);
}


/***************************************************************************
 * BF_Draw -- old path whole-rectangle blitters (BF_Copy .. BF_Predator_*) *
 *                                                                         *
 *    Per pixel: transparent (0) pixels are skipped with FLAG_TRANS; a      *
 *    predator pixel takes its value from the destination a table offset   *
 *    away; then ghost, then fade. Plain BF_Predator (no other flags) only *
 *    writes predator pixels; every other predator variant writes the      *
 *    source pixel when the pixel isn't a predator one.                    *
 *=========================================================================*/
template<int MODE>
static void BF_Draw(unsigned char * dst, unsigned char const * src, int width, int height,
						int dst_adjust, int src_adjust, BFContext const & ctx)
{
	if (MODE == 0) {
		while (height-- > 0) {
			memcpy(dst, src, width);
			dst += width + dst_adjust;
			src += width + src_adjust;
		}
		return;
	}

	while (height-- > 0) {
		for (int count = width; count > 0; count--, dst++) {
			unsigned char pixel = *src++;

			if ((MODE & FLAG_TRANS) && pixel == 0) continue;

			if (MODE & FLAG_PREDATOR) {
				if (Predator_Step()) {
					pixel = dst[Predator_Offset()];
				} else {
					if (MODE == FLAG_PREDATOR) continue;
				}
			}
			if (MODE & FLAG_GHOST) pixel = Ghost(pixel, *dst, ctx);
			if (MODE & FLAG_FADING) pixel = Fade(pixel, ctx);
			*dst = pixel;
		}
		src += src_adjust;
		dst += dst_adjust;
	}
}


typedef void (*BFFunction)(unsigned char *, unsigned char const *, int, int, int, int, BFContext const &);

static BFFunction const BufferFrameTable[16] = {
	BF_Draw<0>,  BF_Draw<1>,  BF_Draw<2>,  BF_Draw<3>,
	BF_Draw<4>,  BF_Draw<5>,  BF_Draw<6>,  BF_Draw<7>,
	BF_Draw<8>,  BF_Draw<9>,  BF_Draw<10>, BF_Draw<11>,
	BF_Draw<12>, BF_Draw<13>, BF_Draw<14>, BF_Draw<15>
};


/***************************************************************************
 * Single_Line_* -- new path line routines.                                *
 *                                                                         *
 *    Each draws one full (unclipped) line of "width" pixels. They follow  *
 *    the asm routine of the same name, including its differences from     *
 *    the BF_* versions (noted where they matter).                         *
 *=========================================================================*/
typedef void (*LineFunction)(unsigned char *, unsigned char const *, int, BFContext const &);

static void Single_Line_Skip(unsigned char *, unsigned char const *, int, BFContext const &)
{
}


static void Single_Line_Copy(unsigned char * dst, unsigned char const * src, int width, BFContext const &)
{
	memcpy(dst, src, width);
}


static void Single_Line_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const &)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) *dst = pixel;
	}
}


static void Single_Line_Ghost(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		*dst = Ghost(*src++, *dst, ctx);
	}
}


static void Single_Line_Ghost_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) *dst = Ghost(pixel, *dst, ctx);
	}
}


static void Single_Line_Fading(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--) {
		*dst++ = Fade(*src++, ctx);
	}
}


static void Single_Line_Fading_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) *dst = Fade(pixel, ctx);
	}
}


static void Single_Line_Single_Fade(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--) {
		*dst++ = ctx.FadingTable[*src++];
	}
}


static void Single_Line_Single_Fade_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) *dst = ctx.FadingTable[pixel];
	}
}


static void Single_Line_Ghost_Fading(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		*dst = Fade(Ghost(*src++, *dst, ctx), ctx);
	}
}


static void Single_Line_Ghost_Fading_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) *dst = Fade(Ghost(pixel, *dst, ctx), ctx);
	}
}


/*
**	Predator lines: non-predator pixels are left untouched (unlike BF_Predator_Trans, which
**	writes the source pixel there).
*/
static void Single_Line_Predator(unsigned char * dst, unsigned char const *, int width, BFContext const &)
{
	for (; width > 0; width--, dst++) {
		if (Predator_Step()) *dst = dst[Predator_Offset()];
	}
}


static void Single_Line_Predator_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const &)
{
	for (; width > 0; width--, dst++) {
		if (*src++ && Predator_Step()) *dst = dst[Predator_Offset()];
	}
}


/*
**	webcandc: in the asm, the non-predator pixels of these two index the is-translucent table
**	with EAX whose upper bits still hold the jump table address (and _Trans also executes a
**	stray "pop ecx"). The intended behaviour -- ghost the source pixel -- is used instead.
*/
static void Single_Line_Predator_Ghost(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (Predator_Step()) pixel = dst[Predator_Offset()];
		*dst = Ghost(pixel, *dst, ctx);
	}
}


static void Single_Line_Predator_Ghost_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) {
			if (Predator_Step()) pixel = dst[Predator_Offset()];
			*dst = Ghost(pixel, *dst, ctx);
		}
	}
}


/*
**	Asm quirk kept: the predator index is stepped with "and" instead of "add".
*/
static void Single_Line_Predator_Fading(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (Predator_Step()) pixel = dst[Predator_Offset_And_Quirk()];
		*dst = Fade(pixel, ctx);
	}
}


static void Single_Line_Predator_Fading_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		unsigned char pixel = *src++;
		if (pixel) {
			if (Predator_Step()) pixel = dst[Predator_Offset_And_Quirk()];
			*dst = Fade(pixel, ctx);
		}
	}
}


/*
**	Asm quirk kept: "xor eax,eax" right before the fade loop discards the pixel, so these
**	store the faded color 0. (The predator counter still advances.) CC_Draw_Shape never
**	combines predator with ghost+fading, so this path is not reached by the game.
*/
static void Single_Line_Predator_Ghost_Fading(unsigned char * dst, unsigned char const *, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		if (Predator_Step()) Predator_Offset();
		*dst = Fade(0, ctx);
	}
}


static void Single_Line_Predator_Ghost_Fading_Trans(unsigned char * dst, unsigned char const * src, int width, BFContext const & ctx)
{
	for (; width > 0; width--, dst++) {
		if (*src++) {
			if (Predator_Step()) Predator_Offset();
			*dst = Fade(0, ctx);
		}
	}
}


/*
**	AllFlagsJumpTable. For line header bits that are a subset of the drawing flags, every
**	per-flag jump table in the asm agrees with this one, so only this one is needed.
**	Entries 4 and 5 are the "critical fade redirections": they are rebound per call to the
**	single-lookup routines when the fade count is 1.
*/
static LineFunction const AllFlagsJumpTable[32] = {
	Single_Line_Copy,
	Single_Line_Trans,
	Single_Line_Ghost,
	Single_Line_Ghost_Trans,
	Single_Line_Single_Fade,
	Single_Line_Single_Fade_Trans,
	Single_Line_Ghost_Fading,
	Single_Line_Ghost_Fading_Trans,
	Single_Line_Predator,
	Single_Line_Predator_Trans,
	Single_Line_Predator_Ghost,
	Single_Line_Predator_Ghost_Trans,
	Single_Line_Predator_Fading,
	Single_Line_Predator_Fading_Trans,
	Single_Line_Predator_Ghost_Fading,
	Single_Line_Predator_Ghost_Fading_Trans,
	Single_Line_Skip, Single_Line_Skip, Single_Line_Skip, Single_Line_Skip,
	Single_Line_Skip, Single_Line_Skip, Single_Line_Skip, Single_Line_Skip,
	Single_Line_Skip, Single_Line_Skip, Single_Line_Skip, Single_Line_Skip,
	Single_Line_Skip, Single_Line_Skip, Single_Line_Skip, Single_Line_Skip
};


/***************************************************************************
 * Setup_Shape_Header -- create the line header bytes for a shape          *
 *                                                                         *
 *    Records the drawing flags in the header and, per line, which effects *
 *    the line needs: transparent if it has any 0 pixel (skip if it is all *
 *    0), predator/fading if it has any opaque pixel, ghost if it has an   *
 *    opaque pixel that is translucent.                                    *
 *=========================================================================*/
static void Setup_Shape_Header(int pixel_width, int pixel_height, unsigned char const * src,
											KFShapeHeaderType * header, int flags, unsigned char const * is_translucent)
{
	header->draw_flags = flags & KF_HEADER_FLAGS;
	unsigned char * line = (unsigned char *)(header + 1);

	for (int row = pixel_height; row > 0; row--) {
		unsigned char seen = 0;
		int trans_count = 0;

		for (int col = pixel_width; col > 0; col--) {
			unsigned char pixel = *src++;

			if (pixel == 0 && (flags & KF_SHAPE_TRANS)) {
				seen |= BLIT_TRANSPARENT;
				trans_count++;
				continue;
			}
			if (flags & KF_SHAPE_PREDATOR) seen |= BLIT_PREDATOR;
			if ((flags & KF_SHAPE_GHOST) && is_translucent[pixel] != 0xFF) seen |= BLIT_GHOST;
			if (flags & KF_SHAPE_FADING) seen |= BLIT_FADING;
		}

		unsigned char type = 0;
		if (seen & BLIT_TRANSPARENT) {
			type = BLIT_TRANSPARENT;
			if (trans_count == pixel_width) {
				*line++ = BLIT_SKIP;
				continue;
			}
		}
		type |= seen & (BLIT_PREDATOR | BLIT_GHOST | BLIT_FADING);
		*line++ = type;
	}
}


/*
**	Sign bit of a wrapping 32 bit value, as extracted with "shld reg,eax,1".
*/
static inline unsigned Sign_Bit(int value)
{
	return((unsigned)value >> 31);
}


/***************************************************************************
 * Buffer_Frame_To_Page -- Copies a linear buffer to a virtual viewport    *
 *                                                                         *
 * INPUT:	x,y		- position in the viewport to draw at             *
 *		w,h		- dimensions of the shape frame                   *
 *		Buffer	- raw frame, or its ShapeHeaderType when the big  *
 *			  shape buffer is in use                          *
 *		view		- viewport to draw into (caller locks it)         *
 *		flags		- SHAPE_* flags, then the optional arguments      *
 *                                                                         *
 * OUTPUT:      0 (the asm preserved EAX, so its return value was         *
 *              meaningless).                                              *
 *=========================================================================*/
extern "C" long __cdecl Buffer_Frame_To_Page(int x, int y, int w, int h, void *Buffer, GraphicViewPortClass &view, int flags, ...)
{
	if (Buffer == NULL) return(0);

	unsigned char const * src = (unsigned char const *)Buffer;
	KFShapeHeaderType * header = NULL;
	bool use_old_draw = true;

	/*
	**	With the big shape buffer, Buffer points at the shape header; the frame itself is
	**	at an offset into the big or theater buffer.
	*/
	if (UseBigShapeBuffer) {
		header = (KFShapeHeaderType *)Buffer;
		char * start = header->shape_buffer ? TheaterShapeBufferStart : BigShapeBufferStart;
		src = (unsigned char const *)(start + (intptr_t)header->shape_data);
		use_old_draw = false;
	}

	BFContext ctx;
	ctx.IsTranslucent = NULL;
	ctx.Translucent = NULL;
	ctx.FadingTable = NULL;
	ctx.FadingNum = 0;
	int jflags = 0;

	va_list ap;
	va_start(ap, flags);

	if (flags & KF_SHAPE_CENTER) {
		x -= w >> 1;
		y -= h >> 1;
	}

	if (flags & KF_SHAPE_TRANS) {
		jflags |= FLAG_TRANS;
	}

	/*
	**	SHAPE_GHOST: is-translucent table, translucency table follows it.
	*/
	if (flags & KF_SHAPE_GHOST) {
		ctx.IsTranslucent = va_arg(ap, unsigned char const *);
		ctx.Translucent = ctx.IsTranslucent + 256;
		jflags |= FLAG_GHOST;
	}

	/*
	**	Build the line headers on the first draw of this shape. If it was set up with
	**	different flags, fall back to the old drawing routines.
	*/
	if (header != NULL) {
		if (header->draw_flags == 0xFFFFFFFFU) {
			Setup_Shape_Header(w, h, src, header, flags, ctx.IsTranslucent);
		} else {
			if ((unsigned)(flags & KF_HEADER_FLAGS) != header->draw_flags) {
				use_old_draw = true;
			}
		}
	}

	/*
	**	SHAPE_FADING: fading table, fade count (0..63).
	*/
	if (flags & KF_SHAPE_FADING) {
		ctx.FadingTable = va_arg(ap, unsigned char const *);
		ctx.FadingNum = va_arg(ap, int) & 0x3F;
		jflags |= FLAG_FADING;
	}

	/*
	**	SHAPE_PREDATOR: initial table position; a negative value selects the table of
	**	offsets into the next row (built from this viewport's pitch).
	*/
	if (flags & KF_SHAPE_PREDATOR) {
		int offset = (int)((unsigned)va_arg(ap, int) << 1);
		jflags |= FLAG_PREDATOR;

		if (offset >= 0) {
			PredNegative = false;
			PredOffset = offset & PRED_MASK;
		} else {
			PredNegative = true;
			PredOffset = (0U - (unsigned)offset) & PRED_MASK;
		}
		BFPartialCount = 0;
		BFPartialPred = 0x100;

		unsigned pitch = view.Get_Width() + view.Get_XAdd() + view.Get_Pitch();
		for (int index = 0; index < 8; index++) {
			BFPredNegOffsets[index] = (unsigned short)((unsigned short)BFPredNegTable[index] + pitch);
		}
	}

	/*
	**	SHAPE_PARTIAL: predator density.
	*/
	if (flags & KF_SHAPE_PARTIAL) {
		BFPartialPred = va_arg(ap, int) & 0xFF;
	}

	va_end(ap);

	/*
	**	Clip the destination rectangle against the viewport. Outcodes: bit 3 = left of 0,
	**	bit 2 = right of Width, bit 1 = above 0, bit 0 = below Height. Note "right"/"below"
	**	mean > Width/Height, not >=.
	*/
	int vp_width = view.Get_Width();
	int vp_height = view.Get_Height();
	int x1 = x + w;
	int y1 = y + h;
	int scr_x = 0;
	int scr_y = 0;

	unsigned start_code =	(Sign_Bit(x) << 3) | ((Sign_Bit(x - (vp_width + 1)) ^ 1) << 2) |
									(Sign_Bit(y) << 1) | (Sign_Bit(y - (vp_height + 1)) ^ 1);
	unsigned end_code =		(Sign_Bit(x1) << 3) | ((Sign_Bit(x1 - (vp_width + 1)) ^ 1) << 2) |
									(Sign_Bit(y1) << 1) | (Sign_Bit(y1 - (vp_height + 1)) ^ 1);

	if (start_code & end_code) return(0);

	if (start_code | end_code) {
		use_old_draw = true;
		if (start_code & 8) {
			scr_x = -x;
			x = 0;
		}
		if (start_code & 2) {
			scr_y = -y;
			y = 0;
		}
		if (end_code & 4) x1 = vp_width;
		if (end_code & 1) y1 = vp_height;
	}

	int pitch = view.Get_XAdd() + view.Get_Pitch() + vp_width;
	unsigned char * dst = (unsigned char *)view.Get_Offset() + y * pitch + x;
	int dest_adjust_width = pitch + x - x1;
	int scr_adjust_width = w - x1 + x;
	src += scr_y * w + scr_x;

	/*
	**	New path: whole shape visible, per line routine from the line header.
	*/
	if (!use_old_draw) {
		/*
		**	webcandc: the asm's "dec / jnz" loops would run away on an empty shape.
		*/
		if (w <= 0 || h <= 0) return(0);

		LineFunction table[32];
		memcpy(table, AllFlagsJumpTable, sizeof(table));
		if (flags & KF_SHAPE_FADING) {
			if (ctx.FadingNum != 1) {
				table[4] = Single_Line_Fading;
				table[5] = Single_Line_Fading_Trans;
			}
		}

		unsigned char const * line_header = (unsigned char const *)(header + 1);
		for (int row = 0; row < h; row++) {
			table[line_header[row] & BLIT_ALL](dst, src, w, ctx);
			src += w;
			dst += pitch;
		}
		return(0);
	}

	/*
	**	Old path.
	*/
	int blit_height = y1 - y;
	if (blit_height <= 0) return(0);
	int blit_width = x1 - x;
	if (blit_width <= 0) return(0);

	BufferFrameTable[jflags & 0x0F](dst, src, blit_width, blit_height, dest_adjust_width, scr_adjust_width, ctx);
	return(0);
}
