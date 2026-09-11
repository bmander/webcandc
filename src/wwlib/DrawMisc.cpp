//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free 
// software: you can redistribute it and/or modify it under the terms of 
// the GNU General Public License as published by the Free Software Foundation, 
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed 
// in the hope that it will be useful, but with permitted additional restrictions 
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT 
// distributed with this program. You should have received a copy of the 
// GNU General Public License along with permitted additional restrictions 
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/*
** 
** 
**  Misc asm functions from ww lib
**  ST - 12/19/2018 1:20PM
** 
** 
** 
** 
** 
** 
** 
** 
** 
** 
** 
*/

#include "gbuffer.h"
#include "MISC.H"
#include "wsa.h"		// webcandc: extern "C" prototypes for Apply_XOR_Delta*
#include "palette.h"	// webcandc: extern "C" prototype for Set_Palette_Range

/*
** webcandc: shared helpers for the portable C++ ports of the original x86
** routines in this file.  Pointers, int and long are all 32 bits on wasm32;
** the 32-bit wrap-around arithmetic of the asm is reproduced with unsigned
** arithmetic where it matters.
*/
#include <string.h>
#include <stdint.h>

namespace {

/*
** Read access to the protected members of a GraphicViewPortClass (the asm
** read them directly with [reg]GraphicViewPortClass.Member).
*/
struct WW_GVP_Access : public GraphicViewPortClass {
	static long Offset_Of(void const *vp) {return static_cast<GraphicViewPortClass const *>(vp)->*(&WW_GVP_Access::Offset);}
	static int  Width_Of(void const *vp)  {return static_cast<GraphicViewPortClass const *>(vp)->*(&WW_GVP_Access::Width);}
	static int  Height_Of(void const *vp) {return static_cast<GraphicViewPortClass const *>(vp)->*(&WW_GVP_Access::Height);}
	static int  XAdd_Of(void const *vp)   {return static_cast<GraphicViewPortClass const *>(vp)->*(&WW_GVP_Access::XAdd);}
	static int  Pitch_Of(void const *vp)  {return (int)(static_cast<GraphicViewPortClass const *>(vp)->*(&WW_GVP_Access::Pitch));}
};

inline int WW_Add(int a, int b) {return (int)((unsigned)a + (unsigned)b);}
inline int WW_Sub(int a, int b) {return (int)((unsigned)a - (unsigned)b);}
inline int WW_Mul(int a, int b) {return (int)((unsigned)a * (unsigned)b);}
inline int WW_Neg(int a)        {return (int)(0u - (unsigned)a);}

inline unsigned char *GVP_Base(void const *vp) {return (unsigned char *)(intptr_t)WW_GVP_Access::Offset_Of(vp);}
inline int GVP_Width(void const *vp)  {return WW_GVP_Access::Width_Of(vp);}
inline int GVP_Height(void const *vp) {return WW_GVP_Access::Height_Of(vp);}

/* XAdd + Pitch: bytes from the end of one row to the start of the next. */
inline int GVP_Modulo(void const *vp) {return WW_Add(WW_GVP_Access::XAdd_Of(vp), WW_GVP_Access::Pitch_Of(vp));}

/* Width + XAdd + Pitch: bytes from one row to the next. */
inline int GVP_Stride(void const *vp) {return WW_Add(GVP_Width(vp), GVP_Modulo(vp));}

/*
** Sutherland code exactly as built by the shld sequences of the asm:
**   bit3 = x < 0, bit2 = x > width, bit1 = y < 0, bit0 = y > height
** (bits 2 and 0 are the complemented sign bits of x-(width+1) and y-(height+1)).
*/
inline unsigned WW_Clip_Code(int x, int y, int width, int height)
{
	unsigned code = ((unsigned)x >> 31) << 3;
	code |= ((((unsigned)x - (unsigned)width - 1u) >> 31) ^ 1u) << 2;
	code |= ((unsigned)y >> 31) << 1;
	code |= (((unsigned)y - (unsigned)height - 1u) >> 31) ^ 1u;
	return code;
}

/* Same result as a forward "rep movsb", including overlapping regions. */
inline void WW_Copy_Forward(unsigned char *dst, unsigned char const *src, int count)
{
	if (count <= 0) return;
	uintptr_t d = (uintptr_t)dst;
	uintptr_t s = (uintptr_t)src;
	if (d <= s || d >= s + (unsigned)count) {
		memmove(dst, src, (size_t)count);
	} else {
		while (count--) *dst++ = *src++;
	}
}

/* Same result as a backward (std) "rep movsb" starting at the LAST byte of each region. */
inline void WW_Copy_Backward(unsigned char *dst_last, unsigned char const *src_last, int count)
{
	if (count <= 0) return;
	unsigned char *dst = dst_last - (count - 1);
	unsigned char const *src = src_last - (count - 1);
	uintptr_t d = (uintptr_t)dst;
	uintptr_t s = (uintptr_t)src;
	if (d >= s || d + (unsigned)count <= s) {
		memmove(dst, src, (size_t)count);
	} else {
		while (count--) *dst_last-- = *src_last--;
	}
}

/* imul/idiv pair: 32x32->64 signed multiply, 64/32 signed (truncating) divide. */
inline int WW_Mul_Div(int a, int b, int c)
{
	if (c == 0) return 0;	// would raise #DE on x86; avoid a wasm trap
	return (int)(uint32_t)(((int64_t)a * (int64_t)b) / (int64_t)c);
}

/*
** Buffer_Draw_Line clipping helpers: "set_bits" and the "clip_tbl" jump
** table (a_up / a_dwn / a_lft / a_rgt / nada) of the asm.
*/
struct WW_Line_Clip {
	int MinX, MaxX, MinY, MaxY;

	unsigned Bits(int x, int y) const
	{
		unsigned bits = 0;
		if (y < MinY) bits |= 1;		// up
		if (y > MaxY) bits |= 2;		// down
		if (x < MinX) bits |= 4;		// left
		if (x > MaxX) bits |= 8;		// right
		return bits;
	}

	/* clip_vert: xa'=xa+[(miny-ya)(xb-xa)/(yb-ya)], ya'=miny */
	static void Vert(int edge, int &xa, int &ya, int xb, int yb)
	{
		xa = WW_Add(xa, WW_Mul_Div(WW_Sub(xb, xa), WW_Sub(edge, ya), WW_Sub(yb, ya)));
		ya = edge;
	}

	/* clip_horiz: ya'=ya+[(minx-xa)(yb-ya)/(xb-xa)], xa'=minx */
	static void Horiz(int edge, int &xa, int &ya, int xb, int yb)
	{
		ya = WW_Add(ya, WW_Mul_Div(WW_Sub(edge, xa), WW_Sub(yb, ya), WW_Sub(xb, xa)));
		xa = edge;
	}

	/* Returns true (carry set) if point a was moved. */
	bool Clip(unsigned bits, int &xa, int &ya, int &xb, int &yb) const
	{
		switch (bits) {
			case 1:
			case 9:		// a_up
				Vert(MinY, xa, ya, xb, yb);
				return true;

			case 2:
			case 6:		// a_dwn
				ya = WW_Neg(ya);
				yb = WW_Neg(yb);
				Vert(WW_Neg(MaxY), xa, ya, xb, yb);
				ya = WW_Neg(ya);
				yb = WW_Neg(yb);
				return true;

			case 4:
			case 5:		// a_lft
				Horiz(MinX, xa, ya, xb, yb);
				return true;

			case 8:
			case 10:		// a_rgt
				xa = WW_Neg(xa);
				xb = WW_Neg(xb);
				Horiz(WW_Neg(MaxX), xa, ya, xb, yb);
				xa = WW_Neg(xa);
				xb = WW_Neg(xb);
				return true;

			default:		// nada
				return false;
		}
	}
};

/* Build_Fading_Table: "new = orig - ((orig-target) * fraction)" using the asm's imul dl / shl ax,1 / sub dh,ah. */
inline unsigned char WW_Fade_Gun(unsigned char orig, unsigned char target, unsigned char fraction)
{
	int diff = (signed char)(unsigned char)(orig - target);				// al = (orig-target)
	unsigned ax = (unsigned)(diff * (int)(signed char)fraction) & 0xFFFFu;	// imul dl
	ax = (ax << 1) & 0xFFFFu;													// shl ax,1
	return (unsigned char)(orig - (ax >> 8));									// sub dh,ah
}

/* Build_Fading_Table: squared difference of one gun (8-bit signed difference, imul ah). */
inline unsigned WW_Gun_Distance(unsigned char gun, unsigned char ideal)
{
	int diff = (signed char)(unsigned char)(gun - ideal);
	return (unsigned)(diff * diff);
}

/*
** XOR_Delta_Buffer / Copy_Delta_Buffer: the asm received the target (edi),
** delta (esi) and width (ebx) in registers set up by
** Apply_XOR_Delta_To_Page_Or_Viewport; the C++ ports pass them through here.
*/
unsigned char *XORDelta_Target = 0;
unsigned char const *XORDelta_Source = 0;
unsigned int XORDelta_Width = 0;

void WW_Delta_To_Page(int nextrow, bool do_xor)
{
	unsigned char *dst = XORDelta_Target;			// edi
	unsigned char const *src = XORDelta_Source;	// esi
	unsigned int width = XORDelta_Width;			// ebx: max column
	unsigned int col = 0;							// edx: relative column
	unsigned int code;
	unsigned int count;
	bool run;

	for (;;) {
		code = *src++;					// get delta source byte

		if (code == 0) {
			// SHORTRUN
			count = *src++;
			run = true;
		} else if ((code & 0x80) == 0) {
			// SHORTDUMP
			count = code;
			run = false;
		} else {
			// By now, we know it must be a LONGDUMP, SHORTSKIP, LONGRUN, or LONGSKIP
			code -= 0x80;
			if (code == 0) {
				code = src[0] | (src[1] << 8);	// get word code
				src += 2;
				if (code == 0) break;			// long count of zero means stop
				if (code & 0x8000) {
					code -= 0x8000;
					if (code & 0x4000) {
						// LONGRUN
						count = code - 0x4000;
						run = true;
					} else {
						// LONGDUMP
						count = code;
						run = false;
					}
					code = 0;
				}
			}
			if (code != 0) {
				// SHORTSKIP AND LONGSKIP
				dst -= col;						// go back to beginning or row.
				col += code;					// incriment our count on current row
				while (col >= width) {			// are we past the end of the row
					col -= width;
					dst += nextrow;				// jump to start of next row
				}
				dst += col;						// get to correct position in row.
				continue;
			}
		}

		if (run) {
			unsigned char value = *src++;		// get XOR byte
			for (; count; count--) {
				if (do_xor) {
					*dst ^= value;
				} else {
					*dst = value;
				}
				col++;
				dst++;
				if (col == width) {				// are we at the final column
					dst -= width;
					col = 0;
					dst += nextrow;
				}
			}
		} else {
			for (; count; count--) {
				if (do_xor) {
					*dst ^= *src;
				} else {
					*dst = *src;
				}
				src++;
				col++;
				dst++;
				if (col == width) {
					dst -= col;
					col = 0;
					dst += nextrow;
				}
			}
		}
	}
}

}

IconCacheClass::IconCacheClass (void)
{
	IsCached			=FALSE;
	SurfaceLost		=FALSE;
	DrawFrequency	=0;
	CacheSurface	=NULL;
	IconSource		=NULL;
}

IconCacheClass::~IconCacheClass (void)
{
}		  

IconCacheClass	CachedIcons[MAX_CACHED_ICONS];

extern "C"{
IconSetType		IconSetList[MAX_ICON_SETS];
short				IconCacheLookup[MAX_LOOKUP_ENTRIES];
}

int		CachedIconsDrawn=0;		//Counter of number of cache hits
int		UnCachedIconsDrawn=0;	//Counter of number of cache misses
BOOL	CacheMemoryExhausted;	//Flag set if we have run out of video RAM


void Invalidate_Cached_Icons (void) {}
void Restore_Cached_Icons (void) {}
void Register_Icon_Set (void *icon_data , BOOL pre_cache) {};

//
// Prototypes for assembly language procedures in STMPCACH.ASM
//
extern "C" void __cdecl Clear_Icon_Pointers (void) {};
extern "C" void __cdecl Cache_Copy_Icon (void const *icon_ptr ,void * , int) {};
extern "C" int __cdecl Is_Icon_Cached (void const *icon_data , int icon) {return -1;};
extern "C" int __cdecl Get_Icon_Index (void *icon_ptr) {return 0;};
extern "C" int __cdecl Get_Free_Index (void) {return 0;};
extern "C" BOOL __cdecl Cache_New_Icon (int icon_index, void *icon_ptr) {return -1;};
extern "C" int __cdecl Get_Free_Cache_Slot(void) {return -1;}

void IconCacheClass::Draw_It (LPDIRECTDRAWSURFACE dest_surface , int x_pixel, int y_pixel, int window_left , int window_top , int window_width , int window_height) {}



extern	int	CachedIconsDrawn;
extern	int	UnCachedIconsDrawn;


extern "C" void __cdecl Set_Font_Palette_Range(void const *palette, INT start_idx, INT end_idx)
{
}		  


/*
;***************************************************************************
;* VVC::DRAW_LINE -- Scales a virtual viewport to another virtual viewport *
;*                                                                         *
;* INPUT:	WORD sx_pixel 	- the starting x pixel position		   *
;*		WORD sy_pixel	- the starting y pixel position		   *
;*		WORD dx_pixel	- the destination x pixel position	   *
;*		WORD dy_pixel   - the destination y pixel position	   *
;*		WORD color      - the color of the line to draw		   *
;*                                                                         *
;* Bounds Checking: Compares sx_pixel, sy_pixel, dx_pixel and dy_pixel	   *
;*       with the graphic viewport it has been assigned to.		   *
;*                                                                         *
;* HISTORY:                                                                *
;*   06/16/1994 PWG : Created.                                             *
;*   08/30/1994 IML : Fixed clipping bug.				   *
;*=========================================================================*
	PROC	Buffer_Draw_Line C NEAR
	USES	eax,ebx,ecx,edx,esi,edi
*/

void __cdecl Buffer_Draw_Line(void *this_object, int sx, int sy, int dx, int dy, unsigned char color)
{
	// webcandc: C++ port of the original x86 routine
	WW_Line_Clip clip;
	clip.MinX = 0;
	clip.MinY = 0;
	clip.MaxX = GVP_Width(this_object) - 1;		// max pixels are tested inclusively
	clip.MaxY = GVP_Height(this_object) - 1;
	int bpr = GVP_Stride(this_object);

	/*
	** The asm kept the end points in (eax,ebx) and (ecx,edx) and swapped them
	** while clipping; the names below follow the registers so the point that
	** ends up as the start of the line is the same as in the original.
	*/
	int x0 = sx;		// eax
	int y0 = sy;		// ebx
	int x1 = dx;		// ecx
	int y1 = dy;		// edx
	int tmp;

	//;*==================================================================
	//;* This is the section that "pushes" the line into bounds.
	//;*==================================================================
	if (x0 < clip.MinX || x0 > clip.MaxX || y0 < clip.MinY || y0 > clip.MaxY ||
		 x1 < clip.MinX || x1 > clip.MaxX || y1 < clip.MinY || y1 > clip.MaxY) {
		for (;;) {
			// clip_it:
			unsigned bits_prev = clip.Bits(x0, y0);		// edi
			tmp = x0; x0 = x1; x1 = tmp;
			tmp = y0; y0 = y1; y1 = tmp;
			unsigned bits = clip.Bits(x0, y0);			// esi
			if ((bits_prev | bits) == 0) break;			// on_screen
			if (bits_prev & bits) return;					// off_screen
			if (clip.Clip(bits, x0, y0, x1, y1)) continue;
			tmp = x0; x0 = x1; x1 = tmp;
			tmp = y0; y0 = y1; y1 = tmp;
			clip.Clip(bits_prev, x0, y0, x1, y1);
		}
	}

	//;*==================================================================
	//;* Draw the line to the screen.
	//;*==================================================================
	unsigned char *base = GVP_Base(this_object);
	unsigned char *line;
	int count;

	if (y1 == y0) {
		//;* Special case routine for horizontal line draws
		if (!(x0 < x1)) {
			tmp = x0; x0 = x1; x1 = tmp;
		}
		count = WW_Add(WW_Sub(x1, x0), 1);
		line = base + WW_Mul(bpr, y0) + x0;
		if (count > 0) memset(line, color, (size_t)count);
		return;
	}

	int ddy = WW_Sub(y1, y0);
	if (!(y1 > y0)) {				// not drawn down, so reverse the line
		ddy = WW_Neg(ddy);
		tmp = x0; x0 = x1; x1 = tmp;
		y0 = WW_Sub(y0, ddy);
	}

	line = base + WW_Mul(bpr, y0);
	int adder = 1;					// assume a right mover
	int ddx = WW_Sub(x1, x0);
	if (ddx == 0) {
		//;* a special case routine for vertical line draws
		count = WW_Add(ddy, 1);
		line += x0;
		do {
			*line = color;
			line += bpr;
		} while (--count != 0);
		return;
	}
	if (!(x1 > x0)) {
		ddx = WW_Neg(ddx);			// negate for actual pixel length
		adder = -1;					// negate counter to move left
	}
	line += x0;

	int greater;
	int lesser;
	int accum;
	int old;
	if (ddx >= ddy) {
		// horiz: ecx=counter; edx=lesser; edi=greater; esi=adder
		greater = ddx;
		lesser = ddy;
		count = greater;
		accum = (int)((unsigned)greater >> 1);
		for (;;) {
			*line = color;
			if (--count < 0) break;		// end of line
			line += adder;
			old = accum;
			accum = WW_Sub(accum, lesser);
			if (old >= lesser) continue;
			accum = WW_Add(accum, greater);
			line += bpr;				// goto next line
		}
	} else {
		// vert: the adder is conditional and the inc constant
		greater = ddy;
		lesser = ddx;
		count = greater;
		accum = (int)((unsigned)greater >> 1);
		for (;;) {
			*line = color;
			if (--count < 0) break;
			line += bpr;
			old = accum;
			accum = WW_Sub(accum, lesser);
			if (old >= lesser) continue;
			accum = WW_Add(accum, greater);
			line += adder;				// next pixel over
		}
	}
}





/*
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Westwood 32 bit Library                  *
;*                                                                         *
;*                    File Name : DRAWLINE.ASM                             *
;*                                                                         *
;*                   Programmer : Phil W. Gorrow                           *
;*                                                                         *
;*                   Start Date : June 16, 1994                            *
;*                                                                         *
;*                  Last Update : August 30, 1994   [IML]                  *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;*   VVC::Scale -- Scales a virtual viewport to another virtual viewport   *
;*   Normal_Draw -- jump loc for drawing  scaled line of normal pixel      *
;*   __DRAW_LINE -- Assembly routine to draw a line                        *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *

IDEAL
P386
MODEL USE32 FLAT

INCLUDE ".\drawbuff.inc"
INCLUDE ".\gbuffer.inc"


CODESEG
*/


/*
;***************************************************************************
;* VVC::DRAW_LINE -- Scales a virtual viewport to another virtual viewport *
;*                                                                         *
;* INPUT:	WORD sx_pixel 	- the starting x pixel position		   *
;*		WORD sy_pixel	- the starting y pixel position		   *
;*		WORD dx_pixel	- the destination x pixel position	   *
;*		WORD dy_pixel   - the destination y pixel position	   *
;*		WORD color      - the color of the line to draw		   *
;*                                                                         *
;* Bounds Checking: Compares sx_pixel, sy_pixel, dx_pixel and dy_pixel	   *
;*       with the graphic viewport it has been assigned to.		   *
;*                                                                         *
;* HISTORY:                                                                *
;*   06/16/1994 PWG : Created.                                             *
;*   08/30/1994 IML : Fixed clipping bug.				   *
;*=========================================================================*
	PROC	Buffer_Draw_Line C NEAR
	USES	eax,ebx,ecx,edx,esi,edi

	;*==================================================================
	;* Define the arguements that the function takes.
	;*==================================================================
	ARG	this_object:DWORD	; associated graphic view port
	ARG	x1_pixel:DWORD		; the start x pixel position
	ARG	y1_pixel:DWORD		; the start y pixel position
	ARG	x2_pixel:DWORD		; the dest x pixel position
	ARG	y2_pixel:DWORD		; the dest y pixel position
	ARG	color:DWORD		; the color we are drawing

	;*==================================================================
	;* Define the local variables that we will use on the stack
	;*==================================================================
	LOCAL	clip_min_x:DWORD
	LOCAL	clip_max_x:DWORD
	LOCAL	clip_min_y:DWORD
	LOCAL	clip_max_y:DWORD
	LOCAL	clip_var:DWORD
	LOCAL	accum:DWORD
	LOCAL	bpr:DWORD

	;*==================================================================
	;* Take care of find the clip minimum and maximums
	;*==================================================================
	mov	ebx,[this_object]
	xor	eax,eax
	mov	[clip_min_x],eax
	mov	[clip_min_y],eax
	mov	eax,[(GraphicViewPort ebx).GVPWidth]
	mov	[clip_max_x],eax
	add	eax,[(GraphicViewPort ebx).GVPXAdd]
	add	eax,[(GraphicViewPort ebx).GVPPitch]
	mov	[bpr],eax
	mov	eax,[(GraphicViewPort ebx).GVPHeight]
	mov	[clip_max_y],eax

	;*==================================================================
	;* Adjust max pixels as they are tested inclusively.
	;*==================================================================
	dec	[clip_max_x]
	dec	[clip_max_y]

	;*==================================================================
	;* Set the registers with the data for drawing the line
	;*==================================================================
	mov	eax,[x1_pixel]		; eax = start x pixel position
	mov	ebx,[y1_pixel]		; ebx = start y pixel position
	mov	ecx,[x2_pixel]		; ecx = dest x pixel position
	mov	edx,[y2_pixel]		; edx = dest y pixel position

	;*==================================================================
	;* This is the section that "pushes" the line into bounds.
	;* I have marked the section with PORTABLE start and end to signify
	;* how much of this routine is 100% portable between graphics modes.
	;* It was just as easy to have variables as it would be for constants
	;* so the global vars ClipMaxX,ClipMinY,ClipMaxX,ClipMinY are used
	;* to clip the line (default is the screen)
	;* PORTABLE start
	;*==================================================================

	cmp	eax,[clip_min_x]
	jl	short ??clip_it
	cmp	eax,[clip_max_x]
	jg	short ??clip_it
	cmp	ebx,[clip_min_y]
	jl	short ??clip_it
	cmp	ebx,[clip_max_y]
	jg	short ??clip_it
	cmp	ecx,[clip_min_x]
	jl	short ??clip_it
	cmp	ecx,[clip_max_x]
	jg	short ??clip_it
	cmp	edx,[clip_min_y]
	jl	short ??clip_it
	cmp	edx,[clip_max_y]
	jle	short ??on_screen

	;*==================================================================
	;* Takes care off clipping the line.
	;*==================================================================
??clip_it:
	call	NEAR PTR ??set_bits
	xchg	eax,ecx
	xchg	ebx,edx
	mov	edi,esi
	call	NEAR PTR ??set_bits
	mov	[clip_var],edi
	or	[clip_var],esi
	jz	short ??on_screen
	test	edi,esi
	jne	short ??off_screen
	shl	esi,2
	call	[DWORD PTR cs:??clip_tbl+esi]
	jc	??clip_it
	xchg	eax,ecx
	xchg	ebx,edx
	shl	edi,2
	call	[DWORD PTR cs:??clip_tbl+edi]
	jmp	??clip_it

??on_screen:
	jmp	??draw_it

??off_screen:
	jmp	??out

	;*==================================================================
	;* Jump table for clipping conditions
	;*==================================================================
??clip_tbl	DD	??nada,??a_up,??a_dwn,??nada
		DD	??a_lft,??a_lft,??a_dwn,??nada
		DD	??a_rgt,??a_up,??a_rgt,??nada
		DD	??nada,??nada,??nada,??nada

??nada:
	clc
	retn

??a_up:
	mov	esi,[clip_min_y]
	call	NEAR PTR ??clip_vert
	stc
	retn

??a_dwn:
	mov	esi,[clip_max_y]
	neg	esi
	neg	ebx
	neg	edx
	call	NEAR PTR ??clip_vert
	neg	ebx
	neg	edx
	stc
	retn

	;*==================================================================
	;* xa'=xa+[(miny-ya)(xb-xa)/(yb-ya)]
	;*==================================================================
??clip_vert:
	push	edx
	push	eax
	mov	[clip_var],edx		; clip_var = yb
	sub	[clip_var],ebx		; clip_var = (yb-ya)
	neg	eax			; eax=-xa
	add	eax,ecx			; (ebx-xa)
	mov	edx,esi			; edx=miny
	sub	edx,ebx			; edx=(miny-ya)
	imul	edx
	idiv	[clip_var]
	pop	edx
	add	eax,edx
	pop	edx
	mov	ebx,esi
	retn

??a_lft:
	mov	esi,[clip_min_x]
	call	NEAR PTR ??clip_horiz
	stc
	retn

??a_rgt:
	mov	esi,[clip_max_x]
	neg	eax
	neg	ecx
	neg	esi
	call	NEAR PTR ??clip_horiz
	neg	eax
	neg	ecx
	stc
	retn

	;*==================================================================
	;* ya'=ya+[(minx-xa)(yb-ya)/(xb-xa)]
	;*==================================================================
??clip_horiz:
	push	edx
	mov	[clip_var],ecx		; clip_var = xb
	sub	[clip_var],eax		; clip_var = (xb-xa)
	sub	edx,ebx			; edx = (yb-ya)
	neg	eax			; eax = -xa
	add	eax,esi			; eax = (minx-xa)
	imul	edx			; eax = (minx-xa)(yb-ya)
	idiv	[clip_var]		; eax = (minx-xa)(yb-ya)/(xb-xa)
	add	ebx,eax			; ebx = xa+[(minx-xa)(yb-ya)/(xb-xa)]
	pop	edx
	mov	eax,esi
	retn

	;*==================================================================
	;* Sets the condition bits
	;*==================================================================
??set_bits:
	xor	esi,esi
	cmp	ebx,[clip_min_y]	; if y >= top its not up
	jge	short ??a_not_up
	or	esi,1

??a_not_up:
	cmp	ebx,[clip_max_y]	; if y <= bottom its not down
	jle	short ??a_not_down
	or	esi,2

??a_not_down:
	cmp	eax,[clip_min_x]   	; if x >= left its not left
	jge	short ??a_not_left
	or	esi,4

??a_not_left:
	cmp	eax,[clip_max_x]	; if x <= right its not right
	jle	short ??a_not_right
	or	esi,8

??a_not_right:
	retn

	;*==================================================================
	;* Draw the line to the screen.
	;* PORTABLE end
	;*==================================================================
??draw_it:
	sub	edx,ebx			; see if line is being draw down
	jnz	short ??not_hline	; if not then its not a hline
	jmp	short ??hline		; do special case h line

??not_hline:
	jg	short ??down		; if so there is no need to rev it
	neg	edx			; negate for actual pixel length
	xchg	eax,ecx			; swap x's to rev line draw
	sub	ebx,edx			; get old edx

??down:
	push	edx
	push	eax
	mov	eax,[bpr]
	mul	ebx
	mov	ebx,eax
	mov	eax,[this_object]
	add	ebx,[(GraphicViewPort eax).GVPOffset]
	pop	eax
	pop	edx

	mov	esi,1			; assume a right mover
	sub	ecx,eax			; see if line is right
	jnz	short ??not_vline	; see if its a vertical line
	jmp	??vline

??not_vline:
	jg	short ??right		; if so, the difference = length

??left:
	neg	ecx			; else negate for actual pixel length
	neg	esi			; negate counter to move left

??right:
	cmp	ecx,edx			; is it a horiz or vert line
	jge	short ??horiz		; if ecx > edx then |x|>|y| or horiz

??vert:
	xchg	ecx,edx			; make ecx greater and edx lesser
	mov	edi,ecx			; set greater
	mov	[accum],ecx		; set accumulator to 1/2 greater
	shr	[accum],1

	;*==================================================================
	;* at this point ...
	;* eax=xpos ; ebx=page line offset; ecx=counter; edx=lesser; edi=greater;
	;* esi=adder; accum=accumulator
	;* in a vertical loop the adder is conditional and the inc constant
	;*==================================================================
??vert_loop:
	add	ebx,eax
	mov	eax,[color]

??v_midloop:
	mov	[ebx],al
	dec	ecx
	jl	??out
	add	ebx,[bpr]
	sub	[accum],edx		; sub the lesser
	jge	??v_midloop		; any line could be new
	add	[accum],edi		; add greater for new accum
	add	ebx,esi			; next pixel over
	jmp	??v_midloop

??horiz:
	mov	edi,ecx			; set greater
	mov	[accum],ecx		; set accumulator to 1/2 greater
	shr	[accum],1

	;*==================================================================
	;* at this point ...
	;* eax=xpos ; ebx=page line offset; ecx=counter; edx=lesser; edi=greater;
	;* esi=adder; accum=accumulator
	;* in a vertical loop the adder is conditional and the inc constant
	;*==================================================================
??horiz_loop:
	add	ebx,eax
	mov	eax,[color]

??h_midloop:
	mov	[ebx],al
	dec	ecx				; dec counter
	jl	??out				; end of line
	add	ebx,esi
	sub     [accum],edx			; sub the lesser
	jge	??h_midloop
	add	[accum],edi			; add greater for new accum
	add	ebx,[bpr]			; goto next line
	jmp	??h_midloop

	;*==================================================================
	;* Special case routine for horizontal line draws
	;*==================================================================
??hline:
	cmp	eax,ecx			; make eax < ecx
	jl	short ??hl_ac
	xchg	eax,ecx

??hl_ac:
	sub	ecx,eax			; get len
	inc	ecx

	push	edx
	push	eax
	mov	eax,[bpr]
	mul	ebx
	mov	ebx,eax
	mov	eax,[this_object]
	add	ebx,[(GraphicViewPort eax).GVPOffset]
	pop	eax
	pop	edx
	add	ebx,eax
	mov	edi,ebx
	cmp	ecx,15
	jg	??big_line
	mov	al,[byte color]
	rep	stosb			; write as many words as possible
	jmp	short ??out		; get outt


??big_line:
	mov	al,[byte color]
	mov	ah,al
	mov     ebx,eax
	shl	eax,16
	mov	ax,bx
	test	edi,3
	jz	??aligned
	mov	[edi],al
	inc	edi
	dec	ecx
	test	edi,3
	jz	??aligned
	mov	[edi],al
	inc	edi
	dec	ecx
	test	edi,3
	jz	??aligned
	mov	[edi],al
	inc	edi
	dec	ecx

??aligned:
	mov	ebx,ecx
	shr	ecx,2
	rep	stosd
	mov	ecx,ebx
	and	ecx,3
	rep	stosb
	jmp	??out


	;*==================================================================
	;* a special case routine for vertical line draws
	;*==================================================================
??vline:
	mov	ecx,edx			; get length of line to draw
	inc	ecx
	add	ebx,eax
	mov	eax,[color]

??vl_loop:
	mov	[ebx],al		; store bit
	add	ebx,[bpr]
	dec	ecx
	jnz	??vl_loop

??out:
	ret
	ENDP	Buffer_Draw_Line


*/















/*

;***************************************************************************
;* GVPC::FILL_RECT -- Fills a rectangular region of a graphic view port	   *
;*                                                                         *
;* INPUT:	WORD the left hand x pixel position of region		   *
;*		WORD the upper x pixel position of region		   *
;*		WORD the right hand x pixel position of region		   *
;*		WORD the lower x pixel position of region		   *
;*		UBYTE the color (optional) to clear the view port to	   *
;*                                                                         *
;* OUTPUT:      none                                                       *
;*                                                                         *
;* NOTE:	This function is optimized to handle viewport with no XAdd *
;*		value.  It also handles DWORD aligning the destination	   *
;*		when speed can be gained by doing it.			   *
;* HISTORY:                                                                *
;*   06/07/1994 PWG : Created.                                             *
;*=========================================================================*
*/ 

/*
;******************************************************************************
; Much testing was done to determine that only when there are 14 or more bytes
; being copied does it speed the time it takes to do copies in this algorithm.
; For this reason and because 1 and 2 byte copies crash, is the special case
; used.  SKB 4/21/94.  Tested on 486 66mhz.  Copied by PWG 6/7/04.
*/ 
#define OPTIMAL_BYTE_COPY	14


void __cdecl Buffer_Fill_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
/*
	;*===================================================================
	;* define the arguements that our function takes.
	;*===================================================================
	ARG    	this_object:DWORD			; this is a member function
	ARG	x1_pixel:WORD
	ARG	y1_pixel:WORD
	ARG	x2_pixel:WORD
	ARG	y2_pixel:WORD
	ARG    	color:BYTE			; what color should we clear to
*/
	
	void *this_object = thisptr;
	int x1_pixel = sx;
	int y1_pixel = sy;
	int x2_pixel = dx;
	int y2_pixel = dy;
	
/*
	;*===================================================================
	; Define some locals so that we can handle things quickly
	;*===================================================================
	LOCAL	VPwidth:DWORD		; the width of the viewport
	LOCAL	VPheight:DWORD		; the height of the viewport
	LOCAL	VPxadd:DWORD		; the additional x offset of viewport
	LOCAL	VPbpr:DWORD		; the number of bytes per row of viewport
*/

	// webcandc: C++ port of the original x86 routine
	int VPwidth = GVP_Width(this_object);
	int VPheight = GVP_Height(this_object);
	int VPxadd = GVP_Modulo(this_object);		// xadd + extra pitch of direct draw surface
	int VPbpr = WW_Add(VPwidth, VPxadd);

	int x = x1_pixel;		// eax
	int y = y1_pixel;		// ebx
	int w = x2_pixel;		// ecx
	int h = y2_pixel;		// edx
	int t;

	//;* Convert the x2 and y2 pixel to a width and height
	if (!(x < w)) {
		t = x; x = w; w = t;
	}
	w = WW_Sub(w, x);
	if (!(y < h)) {
		t = y; y = h; h = t;
	}
	h = WW_Sub(h, y);
	w = WW_Add(w, 1);
	h = WW_Add(h, 1);

	//;* Bounds check source X.
	if (x >= VPwidth) return;					// starts off screen, then later
	if (!((unsigned)x < (unsigned)VPwidth)) {	// if it's not negative, it's ok
		w = WW_Add(w, x);						// Reduce width (add in negative src X).
		x = 0;									// Clip to left of screen.
	}

	//;* Bounds check source Y.
	if (y >= VPheight) return;
	if (!((unsigned)y < (unsigned)VPheight)) {
		h = WW_Add(h, y);
		y = 0;
	}

	//;* Bounds check width versus width of source and dest view ports
	t = WW_Sub(WW_Sub(VPwidth, x), w);			// Pixel width undershoot.
	if (t < 0) w = WW_Add(w, t);				// Reduce width to screen limits.

	//;* Bounds check height versus height of source view port
	t = WW_Sub(WW_Sub(VPheight, y), h);
	if (t < 0) h = WW_Add(h, t);

	//;* Perform the last minute checks on the width and height
	if (w == 0 || h == 0) return;
	if ((unsigned)w > (unsigned)VPwidth) return;
	if ((unsigned)h > (unsigned)VPheight) return;

	//;* Get the offset into the virtual viewport.
	unsigned char *dst = GVP_Base(this_object) + WW_Mul(y, VPbpr) + x;
	VPxadd = WW_Sub(VPxadd, WW_Sub(w, VPwidth));	// modify xadd value to include clipped width bytes

	// If there is no row offset then adjust the width to be the size of
	//   the entire viewport and adjust the height to be 1
	if (VPxadd == 0) {
		w = WW_Mul(w, h);
		h = 1;
	}

	// (The asm dword-aligned rows of OPTIMAL_BYTE_COPY bytes or more; the
	// bytes written are the same.)
	do {
		memset(dst, color, (size_t)(unsigned)w);
		dst += (unsigned)w;
		dst += VPxadd;
	} while (--h != 0);
}




/*
;***************************************************************************
;* VVPC::CLEAR -- Clears a virtual viewport instance                       *
;*                                                                         *
;* INPUT:	UBYTE the color (optional) to clear the view port to	   *
;*                                                                         *
;* OUTPUT:      none                                                       *
;*                                                                         *
;* NOTE:	This function is optimized to handle viewport with no XAdd *
;*		value.  It also handles DWORD aligning the destination	   *
;*		when speed can be gained by doing it.			   *
;* HISTORY:                                                                *
;*   06/07/1994 PWG : Created.                                             *
;*   08/23/1994 SKB : Clear the direction flag to always go forward.       *
;*=========================================================================*
*/
void	__cdecl Buffer_Clear(void *this_object, unsigned char color)
{
	// webcandc: C++ port of the original x86 routine
	unsigned char *dst = GVP_Base(this_object);
	int height = GVP_Height(this_object);
	int width = GVP_Width(this_object);
	int modulo = GVP_Modulo(this_object);		// XAdd + Pitch: add for each line

	// (The asm dword-aligned rows of OPTIMAL_BYTE_COPY bytes or more; the
	// bytes written are the same.)
	for (; height > 0; height--) {
		if (width > 0) memset(dst, color, (size_t)width);
		dst += width;
		dst += modulo;
	}
}














BOOL __cdecl Linear_Blit_To_Linear(	void *this_object, void * dest, int x_pixel, int y_pixel, int dest_x0, int dest_y0, int pixel_width, int pixel_height, BOOL trans)
{
/*
	;*===================================================================
	;* define the arguements that our function takes.
	;*===================================================================
	ARG    	this_object :DWORD		; this is a member function
	ARG	dest        :DWORD		; what are we blitting to
	ARG	x_pixel     :DWORD		; x pixel position in source
	ARG	y_pixel     :DWORD		; y pixel position in source
	ARG	dest_x0     :dword
	ARG	dest_y0     :dword
	ARG	pixel_width :DWORD		; width of rectangle to blit
	ARG	pixel_height:DWORD		; height of rectangle to blit
	ARG	trans       :DWORD			; do we deal with transparents?

	;*===================================================================
	; Define some locals so that we can handle things quickly
	;*===================================================================
	LOCAL 	x1_pixel :dword
	LOCAL	y1_pixel :dword
	LOCAL	dest_x1 : dword
	LOCAL	dest_y1 : dword
	LOCAL	scr_ajust_width:DWORD
	LOCAL	dest_ajust_width:DWORD
        LOCAL	source_area :  dword
        LOCAL	dest_area :  dword
*/
	
	// webcandc: C++ port of the original x86 routine
	int	x1_pixel;
	int	y1_pixel;
	int	dest_x1;
	int	dest_y1;
	int	scr_adjust_width;
	int	dest_adjust_width;
	int	source_area;
	int	dest_area;
	unsigned code0;
	unsigned code1;

	// This Clipping algorithm is a derivation of the very well known
	// Cohen-Sutherland Line-Clipping test (see Clip_Rect).

	// Clip Source Rectangle against source Window boundaries.
	int win_w = GVP_Width(this_object);
	int win_h = GVP_Height(this_object);
	x1_pixel = WW_Add(x_pixel, pixel_width);
	y1_pixel = WW_Add(y_pixel, pixel_height);
	code0 = WW_Clip_Code(x_pixel, y_pixel, win_w, win_h);
	code1 = WW_Clip_Code(x1_pixel, y1_pixel, win_w, win_h);
	if (code0 & code1) return 0;			// the rectangle is outside
	if (code0 | code1) {
		if (code0 & 8) x_pixel = 0;
		if (code0 & 2) y_pixel = 0;
		if (code1 & 4) x1_pixel = win_w;
		if (code1 & 1) y1_pixel = win_h;
	}

	// Clip Source Rectangle against destination Window boundaries.
	// build the destination rectangle before clipping
	dest_x1 = WW_Add(WW_Sub(dest_x0, x_pixel), x1_pixel);
	dest_y1 = WW_Add(WW_Sub(dest_y0, y_pixel), y1_pixel);
	win_w = GVP_Width(dest);
	win_h = GVP_Height(dest);
	code0 = WW_Clip_Code(dest_x0, dest_y0, win_w, win_h);
	code1 = WW_Clip_Code(dest_x1, dest_y1, win_w, win_h);
	if (code0 & code1) return 0;
	if (code0 | code1) {
		if (code0 & 8) {
			x_pixel = WW_Sub(x_pixel, dest_x0);
			dest_x0 = 0;
		}
		if (code0 & 2) {
			y_pixel = WW_Sub(y_pixel, dest_y0);
			dest_y0 = 0;
		}
		if (code1 & 4) {
			x1_pixel = WW_Sub(x1_pixel, WW_Sub(dest_x1, win_w));
			dest_x1 = win_w;
		}
		if (code1 & 1) {
			y1_pixel = WW_Sub(y1_pixel, WW_Sub(dest_y1, win_h));
			dest_y1 = win_h;
		}
	}

	// Here is where we do the actual blit
	source_area = GVP_Stride(this_object);
	unsigned char *src = GVP_Base(this_object) + WW_Mul(source_area, y_pixel) + x_pixel;
	scr_adjust_width = WW_Sub(WW_Add(source_area, x_pixel), x1_pixel);

	dest_area = GVP_Stride(dest);
	unsigned char *dst = GVP_Base(dest) + WW_Mul(dest_area, dest_y0) + dest_x0;

	if (dest_x1 <= dest_x0) return 0;
	int width = WW_Sub(dest_x1, dest_x0);
	dest_adjust_width = WW_Sub(dest_area, width);

	if (dest_y1 <= dest_y0) return 0;
	int height = WW_Sub(dest_y1, dest_y0);

	if (src == dst) return 0;

	if ((intptr_t)src > (intptr_t)dst) {
		// ********************************************************************
		// Forward bitblit
		if (trans & 1) {
			do {
				for (int i = 0; i < width; i++) {
					unsigned char pixel = src[i];
					if (pixel) dst[i] = pixel;
				}
				src += width;
				dst += width;
				src += scr_adjust_width;
				dst += dest_adjust_width;
			} while (--height != 0);
		} else {
			do {
				WW_Copy_Forward(dst, src, width);
				src += width;
				dst += width;
				src += scr_adjust_width;
				dst += dest_adjust_width;
			} while (--height != 0);
		}
	} else {
		// ************************************************************************
		// backward bitblit (start from the last byte of the last row)
		height--;
		src += width;
		src += WW_Mul(source_area, height) - 1;
		dst += width;
		dst += WW_Mul(dest_area, height) - 1;
		if (trans & 1) {
			do {
				for (int i = 0; i < width; i++) {
					unsigned char pixel = src[-i];
					if (pixel) dst[-i] = pixel;
				}
				src -= source_area;
				dst -= dest_area;
			} while (--height >= 0);
		} else {
			do {
				WW_Copy_Backward(dst, src, width);
				src -= source_area;
				dst -= dest_area;
			} while (--height >= 0);
		}
	}
	return 0;
}












/*
;***************************************************************************
;* VVC::SCALE -- Scales a virtual viewport to another virtual viewport     *
;*                                                                         *
;* INPUT:                                                                  *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* WARNINGS:                                                               *
;*                                                                         *
;* HISTORY:                                                                *
;*   06/16/1994 PWG : Created.                                             *
;*=========================================================================*
	PROC	Linear_Scale_To_Linear C NEAR
	USES	eax,ebx,ecx,edx,esi,edi
*/

// Ran out of registers so had to use ebp. ST - 12/19/2018 6:22PM
#pragma warning (push)
#pragma warning (disable : 4731)

BOOL __cdecl Linear_Scale_To_Linear(void *this_object, void *dest, int src_x, int src_y, int dst_x, int dst_y, int src_width, int src_height, int dst_width, int dst_height, BOOL trans, char *remap)
{
/*			  

	;*===================================================================
	;* Define the arguements that our function takes.
	;*===================================================================
	ARG	this_object:DWORD		; pointer to source view port
	ARG	dest:DWORD		; pointer to destination view port
	ARG	src_x:DWORD		; source x offset into view port
	ARG	src_y:DWORD		; source y offset into view port
	ARG	dst_x:DWORD		; dest x offset into view port
	ARG	dst_y:DWORD		; dest y offset into view port
	ARG	src_width:DWORD		; width of source rectangle
	ARG	src_height:DWORD	; height of source rectangle
	ARG	dst_width:DWORD		; width of dest rectangle
	ARG	dst_height:DWORD	; width of dest height
	ARG	trans:DWORD		; is this transparent?
	ARG	remap:DWORD		; pointer to table to remap source

	;*===================================================================
	;* Define local variables to hold the viewport characteristics
	;*===================================================================
	local	src_x0 : dword
	local	src_y0 : dword
	local	src_x1 : dword
	local	src_y1 : dword

	local	dst_x0 : dword
	local	dst_y0 : dword
	local	dst_x1 : dword
	local	dst_y1 : dword

	local	src_win_width : dword
	local	dst_win_width : dword
	local	dy_intr : dword
	local	dy_frac : dword
	local	dy_acc  : dword
	local	dx_frac : dword

	local	counter_x     : dword
	local	counter_y     : dword
	local	remap_counter :dword
	local	entry : dword
*/
	
	// webcandc: C++ port of the original x86 routine
	int src_x0;
	int src_y0;
	int src_x1;
	int src_y1;

	int dst_x0;
	int dst_y0;
	int dst_x1;
	int dst_y1;

	int src_win_width;
	int dst_win_width;
	int dy_intr;
	int dy_frac;
	int dy_acc;
	unsigned dx_intr;
	unsigned dx_frac;

	int counter_x;
	int counter_y;
	unsigned code0;
	unsigned code1;
	int win_w;
	int win_h;

	//;* Check for scale error when to or from size 0,0
	if (dst_width == 0 || dst_height == 0 || src_width == 0 || src_height == 0) return TRUE;

	src_x0 = src_x;
	src_y0 = src_y;
	src_x1 = WW_Add(src_x, src_width);
	src_y1 = WW_Add(src_y, src_height);

	dst_x0 = dst_x;
	dst_y0 = dst_y;
	dst_x1 = WW_Add(dst_x, dst_width);
	dst_y1 = WW_Add(dst_y, dst_height);

	// Clip Source Rectangle against source Window boundaries.
	win_w = GVP_Width(this_object);
	win_h = GVP_Height(this_object);
	code0 = WW_Clip_Code(src_x0, src_y0, win_w, win_h);
	code1 = WW_Clip_Code(src_x1, src_y1, win_w, win_h);
	if (code0 & code1) return TRUE;
	if (code0 | code1) {
		if (code0 & 8) {
			src_x0 = 0;
			dst_x0 = WW_Add(WW_Mul_Div(WW_Neg(src_x), dst_width, src_width), dst_x);
		}
		if (code0 & 2) {
			src_y0 = 0;
			dst_y0 = WW_Add(WW_Mul_Div(WW_Neg(src_y), dst_height, src_height), dst_y);
		}
		if (code1 & 4) {
			src_x1 = win_w;
			dst_x1 = WW_Add(WW_Mul_Div(WW_Sub(win_w, src_x), dst_width, src_width), dst_x);
		}
		if (code1 & 1) {
			src_y1 = win_h;
			dst_y1 = WW_Add(WW_Mul_Div(WW_Sub(win_h, src_y), dst_height, src_height), dst_y);
		}
	}

	// Clip destination Rectangle against destination Window boundaries.
	win_w = GVP_Width(dest);
	win_h = GVP_Height(dest);
	code0 = WW_Clip_Code(dst_x0, dst_y0, win_w, win_h);
	code1 = WW_Clip_Code(dst_x1, dst_y1, win_w, win_h);
	if (code0 & code1) return TRUE;
	if (code0 | code1) {
		if (code0 & 8) {
			dst_x0 = 0;
			src_x0 = WW_Add(WW_Mul_Div(WW_Neg(dst_x), src_width, dst_width), src_x);
		}
		if (code0 & 2) {
			dst_y0 = 0;
			src_y0 = WW_Add(WW_Mul_Div(WW_Neg(dst_y), src_height, dst_height), src_y);
		}
		if (code1 & 4) {
			dst_x1 = win_w;
			src_x1 = WW_Add(WW_Mul_Div(WW_Sub(win_w, dst_x), src_width, dst_width), src_x);
		}
		if (code1 & 1) {
			dst_y1 = win_h;
			src_y1 = WW_Add(WW_Mul_Div(WW_Sub(win_h, dst_y), src_height, dst_height), src_y);
		}
	}
	(void)src_x1;		// (computed but never used, as in the asm)
	(void)src_y1;

	// do_scaling:
	src_win_width = GVP_Stride(this_object);
	unsigned char const *src = GVP_Base(this_object) + WW_Mul(src_win_width, src_y0) + src_x0;

	dst_win_width = GVP_Stride(dest);
	unsigned char *dst = GVP_Base(dest) + WW_Mul(dst_win_width, dst_y0) + dst_x0;

	// Vertical step: src_height / dst_height (edx:eax with edx = 0).
	{
		int64_t num = (int64_t)(uint32_t)src_height;
		dy_intr = WW_Mul((int)(uint32_t)(num / dst_height), src_win_width);
		dy_frac = (int)(num % dst_height);
		dy_acc = WW_Neg(dst_height);
	}

	// Horizontal step: (src_width << 16) / dst_width as 16.16 fixed point;
	// the fraction is kept in the top 16 bits so its carry steps the source.
	{
		int64_t num = (int64_t)(uint32_t)((unsigned)src_width << 16);
		uint32_t q = (uint32_t)(num / dst_width);
		dx_intr = q >> 16;
		dx_frac = q << 16;
	}

	if (dst_y1 <= dst_y0) return TRUE;
	counter_y = WW_Sub(dst_y1, dst_y0);
	if (dst_x1 <= dst_x0) return TRUE;
	counter_x = WW_Sub(dst_x1, dst_x0);

	unsigned char const *table = (unsigned char const *)remap;
	int mode = (trans != 0 ? 2 : 0) | (remap != 0 ? 1 : 0);

	do {
		unsigned char const *s = src;
		unsigned char *d = dst;
		uint32_t acc = 0;
		uint32_t old_acc;
		unsigned char pixel;
		int i;

		switch (mode) {
			case 0:		// normal scale
				for (i = counter_x; i > 0; i--) {
					*d++ = *s;
					old_acc = acc;
					acc += dx_frac;
					s += dx_intr + (acc < old_acc ? 1 : 0);
				}
				break;

			case 1:		// normal scale with remap
				for (i = counter_x; i > 0; i--) {
					*d++ = table[*s];
					old_acc = acc;
					acc += dx_frac;
					s += dx_intr + (acc < old_acc ? 1 : 0);
				}
				break;

			case 2:		// normal scale with transparency
				for (i = counter_x; i > 0; i--) {
					pixel = *s;
					if (pixel) *d = pixel;
					d++;
					old_acc = acc;
					acc += dx_frac;
					s += dx_intr + (acc < old_acc ? 1 : 0);
				}
				break;

			default:	// transparency (checked before the remap) with remap
				for (i = counter_x; i > 0; i--) {
					pixel = *s;
					if (pixel) *d = table[pixel];
					d++;
					old_acc = acc;
					acc += dx_frac;
					s += dx_intr + (acc < old_acc ? 1 : 0);
				}
				break;
		}

		dst += dst_win_width;
		src += dy_intr;

		int64_t sum = (int64_t)dy_acc + (int64_t)dy_frac;
		int next_acc = WW_Add(dy_acc, dy_frac);
		if (sum > 0) {
			src += src_win_width;
			next_acc = WW_Sub(next_acc, dst_height);
		}
		dy_acc = next_acc;
	} while (--counter_y != 0);

	return TRUE;
}


#pragma warning (pop)




















unsigned int LastIconset = 0;
unsigned int StampPtr = 0;	//	DD	0	; Pointer to icon data.

unsigned int IsTrans = 0;	//		DD	0	; Pointer to transparent icon flag table.

unsigned int MapPtr = 0;	//		DD	0	; Pointer to icon map.
unsigned int IconWidth = 0;	//	DD	0	; Width of icon in pixels.
unsigned int IconHeight = 0;	//	DD	0	; Height of icon in pixels.
unsigned int IconSize = 0;		//	DD	0	; Number of bytes for each icon data.
unsigned int IconCount = 0;	//	DD	0	; Number of icons in the set.



#if (0)
LastIconset	DD	0	; Pointer to last iconset initialized.
StampPtr	DD	0	; Pointer to icon data.

IsTrans		DD	0	; Pointer to transparent icon flag table.

MapPtr		DD	0	; Pointer to icon map.
IconWidth	DD	0	; Width of icon in pixels.
IconHeight	DD	0	; Height of icon in pixels.
IconSize	DD	0	; Number of bytes for each icon data.
IconCount	DD	0	; Number of icons in the set.


GLOBAL C	Buffer_Draw_Stamp:near
GLOBAL C	Buffer_Draw_Stamp_Clip:near

; 256 color icon system.
#endif


/*
;***********************************************************
; INIT_STAMPS
;
; VOID cdecl Init_Stamps(VOID *icondata);
;
; This routine initializes the stamp data.
; Bounds Checking: NONE
;
;*
*/ 
extern "C" void __cdecl Init_Stamps(unsigned int icondata)
{

	// webcandc: C++ port of the original x86 routine
	// Verify legality of parameter.
	if (icondata == 0) return;

	// Don't initialize if already initialized to this set (speed reasons).
	if (LastIconset == icondata) return;
	LastIconset = icondata;

	IControl_Type const *control = (IControl_Type const *)(uintptr_t)icondata;

	// Record number of icons in set.
	IconCount = (unsigned short)control->Count;

	// Record width of icon.
	IconWidth = (unsigned short)control->Width;

	// Record height of icon.
	IconHeight = (unsigned short)control->Height;

	// Record size of icon (in bytes).
	IconSize = IconWidth * IconHeight;

	// Record hard pointer to icon map data (always set, as in the asm).
	MapPtr = icondata + (unsigned int)(uintptr_t)control->Map;

	// Record hard pointer to icon data.
	StampPtr = icondata + (unsigned int)(uintptr_t)control->Icons;

	// Record the transparent table.
	IsTrans = icondata + (unsigned int)(uintptr_t)control->TransFlag;
}


/*
;***********************************************************

;***********************************************************
; DRAW_STAMP
;
; VOID cdecl Buffer_Draw_Stamp(VOID *icondata, WORD icon, WORD x_pixel, WORD y_pixel, VOID *remap);
;
; This routine renders the icon at the given coordinate.
;
; The remap table is a 256 byte simple pixel translation table to use when
; drawing the icon.  Transparency check is performed AFTER the remap so it is possible to
; remap valid colors to be invisible (for special effect reasons).
; This routine is fastest when no remap table is passed in.
;*
*/

void __cdecl Buffer_Draw_Stamp(void const *this_object, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap)
{
	unsigned int	modulo = 0;
	unsigned int	iwidth = 0;
	unsigned char	doremap = 0;


/*
		PROC	Buffer_Draw_Stamp C near

		ARG	this_object:DWORD		; this is a member function
		ARG	icondata:DWORD		; Pointer to icondata.
		ARG	icon:DWORD		; Icon number to draw.
		ARG	x_pixel:DWORD		; X coordinate of icon.
		ARG	y_pixel:DWORD		; Y coordinate of icon.
		ARG	remap:DWORD 		; Remap table.

		LOCAL	modulo:DWORD		; Modulo to get to next row.
		LOCAL	iwidth:DWORD		; Icon width (here for speedy access).
		LOCAL	doremap:BYTE		; Should remapping occur?
*/
		
	// webcandc: C++ port of the original x86 routine
	if (icondata == 0) return;

	// Initialize the stamp data if necessary.
	if (LastIconset != (unsigned int)(uintptr_t)icondata) {
		Init_Stamps((unsigned int)(uintptr_t)icondata);
	}

	// Determine if the icon number requested is actually in the set.
	// Perform the logical icon to actual icon number remap if necessary
	// (only the low byte is replaced: mov bl,[edi+ebx]).
	unsigned int icon_num = (unsigned int)icon;
	if (MapPtr != 0) {
		icon_num = (icon_num & ~0xFFu) | ((unsigned char const *)(uintptr_t)MapPtr)[icon_num];
	}
	if (icon_num >= IconCount) return;
	icon = (int)icon_num;			// Updated icon number.

	// If the remap table pointer passed in is NULL, then flag this condition
	// so that the faster (non-remapping) icon draw loop will be used.
	doremap = (remap != 0);

	// Get pointer to position to render icon.
	int stride = GVP_Stride(this_object);
	unsigned char *dst = GVP_Base(this_object) + WW_Mul(stride, y_pixel) + x_pixel;

	// Determine row modulo for advancing to next line.
	modulo = (unsigned int)stride - IconWidth;

	// Setup some working variables.
	unsigned int rows = IconHeight;		// Row counter.
	iwidth = IconWidth;

	// Fetch pointer to start of icon's data.
	unsigned char const *src = (unsigned char const *)(uintptr_t)(StampPtr + icon_num * IconSize);
	unsigned char pixel;
	unsigned int i;

	if (doremap) {
		// Complex icon draw -- extended remap.
		unsigned char const *xlat = (unsigned char const *)remap;
		for (; rows; rows--) {
			for (i = iwidth; i; i--) {
				pixel = xlat[*src++];		// New real color to draw.
				if (pixel) *dst = pixel;	// Transparency skip check.
				dst++;
			}
			dst += (int)modulo;
		}
		return;
	}

	// Check to see if transparent or generic draw is necessary.
	if (((unsigned char const *)(uintptr_t)IsTrans)[icon_num] == 0) {
		// Fast non-transparent icon draw routine (4 rows of whole dwords per pass).
		unsigned int blocks = rows >> 2;
		unsigned int bytes = (iwidth >> 2) * 4;
		for (; blocks; blocks--) {
			for (i = 0; i < 4; i++) {
				memcpy(dst, src, bytes);
				src += bytes;
				dst += bytes;
				dst += (int)modulo;
			}
		}
		return;
	}

	// Transparent icon draw routine -- no extended remap.
	for (; rows; rows--) {
		for (i = iwidth; i; i--) {
			pixel = *src++;
			if (pixel) *dst = pixel;		// Transparency check.
			dst++;
		}
		dst += (int)modulo;
	}
}




/*
;***********************************************************
; DRAW_STAMP_CLIP
;
; VOID cdecl MCGA_Draw_Stamp_Clip(VOID *icondata, WORD icon, WORD x_pixel, WORD y_pixel, VOID *remap, LONG min_x, LONG min_y, LONG max_x, LONG max_y);
;
; This routine renders the icon at the given coordinate.
;
; The remap table is a 256 byte simple pixel translation table to use when
; drawing the icon.  Transparency check is performed AFTER the remap so it is possible to
; remap valid colors to be invisible (for special effect reasons).
; This routine is fastest when no remap table is passed in.
;*
*/	
void __cdecl Buffer_Draw_Stamp_Clip(void const *this_object, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int min_x, int min_y, int max_x, int max_y)
{
	
	
	unsigned int	modulo = 0;
	unsigned int	iwidth = 0;
	unsigned int	skip = 0;
	unsigned char	doremap = 0;
	
		
/*		
	ARG	this_object:DWORD	; this is a member function
	ARG	icondata:DWORD		; Pointer to icondata.
	ARG	icon:DWORD		; Icon number to draw.
	ARG	x_pixel:DWORD		; X coordinate of icon.
	ARG	y_pixel:DWORD		; Y coordinate of icon.
	ARG	remap:DWORD 		; Remap table.
	ARG	min_x:DWORD		; Clipping rectangle boundary
	ARG	min_y:DWORD		; Clipping rectangle boundary
	ARG	max_x:DWORD		; Clipping rectangle boundary
	ARG	max_y:DWORD		; Clipping rectangle boundary

	LOCAL	modulo:DWORD		; Modulo to get to next row.
	LOCAL	iwidth:DWORD		; Icon width (here for speedy access).
	LOCAL	skip:DWORD		; amount to skip per row of icon data
	LOCAL	doremap:BYTE		; Should remapping occur?
*/
	// webcandc: C++ port of the original x86 routine
	if (icondata == 0) return;

	// Initialize the stamp data if necessary.
	if (LastIconset != (unsigned int)(uintptr_t)icondata) {
		Init_Stamps((unsigned int)(uintptr_t)icondata);
	}

	// Determine if the icon number requested is actually in the set.
	// Perform the logical icon to actual icon number remap if necessary
	// (only the low byte is replaced: mov bl,[edi+ebx]).
	unsigned int icon_num = (unsigned int)icon;
	if (MapPtr != 0) {
		icon_num = (icon_num & ~0xFFu) | ((unsigned char const *)(uintptr_t)MapPtr)[icon_num];
	}
	if (icon_num >= IconCount) return;
	icon = (int)icon_num;			// Updated icon number.

	// Setup some working variables.
	int rows = (int)IconHeight;		// Row counter.
	iwidth = IconWidth;

	// Fetch pointer to start of icon's data.
	unsigned char const *src = (unsigned char const *)(uintptr_t)(StampPtr + icon_num * IconSize);

	// Update the clipping window coordinates to be valid maxes instead of width & height
	// , and change the coordinates to be window-relative
	max_x = WW_Add(max_x, min_x);
	x_pixel = WW_Add(x_pixel, min_x);
	max_y = WW_Add(max_y, min_y);
	y_pixel = WW_Add(y_pixel, min_y);

	// See if the icon is within the clipping window
	// First, verify that the icon position is less than the maximums
	if (x_pixel >= max_x) return;
	if (y_pixel >= max_y) return;
	// Now verify that the icon position is >= the minimums
	if (WW_Add(y_pixel, (int)IconHeight) <= min_y) return;
	if (WW_Add(x_pixel, (int)IconWidth) <= min_x) return;

	// Now, clip the x, y, width, and height variables to be within the
	// clipping rectangle
	if (x_pixel < min_x) {
		// x < minx, so must clip
		int delta = WW_Sub(min_x, x_pixel);
		src += delta;						// source ptr += (minx - x)
		iwidth -= (unsigned int)delta;		// icon width -= (minx - x)
		x_pixel = min_x;
	}
	skip = IconWidth - iwidth;

	// Check for x+width > max_x
	if (WW_Add(x_pixel, (int)iwidth) > max_x) {
		// x+width is greater than max_x, so must clip width down
		unsigned int old_width = iwidth;
		iwidth = (unsigned int)WW_Sub(max_x, x_pixel);	// iwidth = max_x - xpixel
		skip += old_width - iwidth;						// skip += (old width - iwidth)
	}

	// check if y < miny
	if (!(min_y <= y_pixel)) {
		int delta = WW_Sub(min_y, y_pixel);
		rows = WW_Sub(rows, delta);					// height -= (miny - y)
		src += (unsigned int)delta * IconWidth;	// icon source ptr += (width * (miny - y))
		y_pixel = min_y;
	}

	// check if (y+height) > max y
	if (WW_Add(y_pixel, rows) > max_y) {
		rows = WW_Sub(max_y, y_pixel);				// height = max_y - y_pixel
	}

	// If the remap table pointer passed in is NULL, then flag this condition
	// so that the faster (non-remapping) icon draw loop will be used.
	doremap = (remap != 0);

	// Get pointer to position to render icon.
	int stride = GVP_Stride(this_object);
	unsigned char *dst = GVP_Base(this_object) + WW_Mul(stride, y_pixel) + x_pixel;

	// Determine row modulo for advancing to next line.
	modulo = (unsigned int)stride - iwidth;

	unsigned char pixel;
	unsigned int i;

	if (doremap) {
		// Complex icon draw -- extended remap.
		unsigned char const *xlat = (unsigned char const *)remap;
		for (; rows > 0; rows--) {
			for (i = iwidth; i; i--) {
				pixel = xlat[*src++];
				if (pixel) *dst = pixel;		// Transparency skip check.
				dst++;
			}
			dst += (int)modulo;
			src += (int)skip;
		}
		return;
	}

	// Check to see if transparent or generic draw is necessary.
	if (((unsigned char const *)(uintptr_t)IsTrans)[icon_num] == 0) {
		// Fast non-transparent icon draw routine.
		for (; rows > 0; rows--) {
			memcpy(dst, src, iwidth);
			dst += iwidth;
			src += iwidth;
			dst += (int)modulo;
			src += (int)skip;
		}
		return;
	}

	// Transparent icon draw routine -- no extended remap.
	for (; rows > 0; rows--) {
		for (i = iwidth; i; i--) {
			pixel = *src++;
			if (pixel) *dst = pixel;			// Transparency check.
			dst++;
		}
		dst += (int)modulo;
		src += (int)skip;
	}
}















	 VOID __cdecl Buffer_Draw_Line(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color);
	 VOID __cdecl Buffer_Fill_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color);
	 VOID __cdecl Buffer_Remap(void * thisptr, int sx, int sy, int width, int height, void *remap);
	 VOID __cdecl Buffer_Fill_Quad(void * thisptr, VOID *span_buff, int x0, int y0, int x1, int y1,
							 	int x2, int y2, int x3, int y3, int color);
	 void __cdecl Buffer_Draw_Stamp(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap);
	 void __cdecl Buffer_Draw_Stamp_Clip(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int ,int,int,int);
	 void * __cdecl Get_Font_Palette_Ptr ( void );


/*
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Westwood 32 bit Library                  *
;*                                                                         *
;*                    File Name : REMAP.ASM                                *
;*                                                                         *
;*                   Programmer : Phil W. Gorrow                           *
;*                                                                         *
;*                   Start Date : July 1, 1994                             *
;*                                                                         *
;*                  Last Update : July 1, 1994   [PWG]                     *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *
*/


VOID __cdecl Buffer_Remap(void * this_object, int sx, int sy, int width, int height, void *remap)
{
/*
	PROC	Buffer_Remap C NEAR
	USES	eax,ebx,ecx,edx,esi,edi

	;*===================================================================
	;* Define the arguements that our function takes.
	;*===================================================================
	ARG	this_object:DWORD
	ARG	x0_pixel:DWORD
	ARG	y0_pixel:DWORD
	ARG	region_width:DWORD
	ARG	region_height:DWORD
	ARG	remap	:DWORD

	;*===================================================================
	; Define some locals so that we can handle things quickly
	;*===================================================================
	local	x1_pixel  : DWORD
	local	y1_pixel  : DWORD
	local	win_width : dword
	local	counter_x : dword
*/

	// webcandc: C++ port of the original x86 routine
	int x0_pixel = sx;
	int y0_pixel = sy;
	int x1_pixel;
	int y1_pixel;
	int win_width;
	int counter_x;
	unsigned code0;
	unsigned code1;

	if (remap == 0) return;

	// Clip Source Rectangle against source Window boundaries.
	int vp_width = GVP_Width(this_object);
	int vp_height = GVP_Height(this_object);
	x1_pixel = WW_Add(x0_pixel, width);
	y1_pixel = WW_Add(y0_pixel, height);
	code0 = WW_Clip_Code(x0_pixel, y0_pixel, vp_width, vp_height);
	code1 = WW_Clip_Code(x1_pixel, y1_pixel, vp_width, vp_height);
	if (code0 & code1) return;
	if (code0 | code1) {
		if (code0 & 8) x0_pixel = 0;
		if (code0 & 2) y0_pixel = 0;
		if (code1 & 4) x1_pixel = vp_width;
		if (code1 & 1) y1_pixel = vp_height;
	}

	// do_remap:
	win_width = GVP_Stride(this_object);
	unsigned char *dst = GVP_Base(this_object) + WW_Mul(win_width, y0_pixel) + x0_pixel;
	if (x1_pixel <= x0_pixel) return;
	counter_x = WW_Sub(x1_pixel, x0_pixel);
	win_width = WW_Sub(win_width, counter_x);

	if (y1_pixel <= y0_pixel) return;
	int rows = WW_Sub(y1_pixel, y0_pixel);
	unsigned char const *table = (unsigned char const *)remap;

	do {
		int i = counter_x;
		do {
			*dst = table[*dst];
			dst++;
		} while (--i != 0);
		dst += win_width;
	} while (--rows != 0);
}















/*
; **************************************************************************
; **   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   *
; **************************************************************************
; *                                                                        *
; *                 Project Name : WSA Support routines			   *
; *                                                                        *
; *                    File Name : XORDELTA.ASM                            *
; *                                                                        *
; *                   Programmer : Scott K. Bowen			   *
; *                                                                        *
; *                  Last Update :May 23, 1994   [SKB]                     *
; *                                                                        *
; *------------------------------------------------------------------------*
; * Functions:                                                             *
;*   Apply_XOR_Delta -- Apply XOR delta data to a buffer.                  *
;*   Apply_XOR_Delta_To_Page_Or_Viewport -- Calls the copy or the XOR funti*
;*   Copy_Delta_buffer -- Copies XOR Delta Data to a section of a page.    *
;*   XOR_Delta_Buffer -- Xor's the data in a XOR Delta format to a page.   *
; * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*

IDEAL
P386
MODEL USE32 FLAT
*/


/*
LOCALS ??

; These are used to call Apply_XOR_Delta_To_Page_Or_Viewport() to setup flags parameter.  If
; These change, make sure and change their values in wsa.cpp.
DO_XOR		equ	0
DO_COPY		equ	1
TO_VIEWPORT	equ	0
TO_PAGE		equ	2

;
; Routines defined in this module
;
;
; UWORD Apply_XOR_Delta(UWORD page_seg, BYTE *delta_ptr);
; PUBLIC Apply_XOR_Delta_To_Page_Or_Viewport(UWORD page_seg, BYTE *delta_ptr, WORD width, WORD copy)
;
;	PROC	C XOR_Delta_Buffer
;	PROC	C Copy_Delta_Buffer
;

GLOBAL 	C Apply_XOR_Delta:NEAR
GLOBAL 	C Apply_XOR_Delta_To_Page_Or_Viewport:NEAR
*/

#define DO_XOR			0
#define DO_COPY		1
#define TO_VIEWPORT	0
#define TO_PAGE		2

void __cdecl XOR_Delta_Buffer(int nextrow);
void __cdecl Copy_Delta_Buffer(int nextrow);


/*
;***************************************************************************
;* APPLY_XOR_DELTA -- Apply XOR delta data to a linear buffer.             *
;*   AN example of this in C is at the botton of the file commented out.   *
;*                                                                         *
;* INPUT:  BYTE *target - destination buffer.                              *
;*         BYTE *delta - xor data to be delta uncompress.                  *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* WARNINGS:                                                               *
;*                                                                         *
;* HISTORY:                                                                *
;*   05/23/1994 SKB : Created.                                             *
;*=========================================================================*
*/
unsigned int __cdecl Apply_XOR_Delta(char *target, char *delta)
{
/* 
PROC	Apply_XOR_Delta C near
	USES 	ebx,ecx,edx,edi,esi
	ARG	target:DWORD 		; pointers.
	ARG	delta:DWORD		; pointers.
*/
	
	// webcandc: C++ port of the original x86 routine
	unsigned char *dst = (unsigned char *)target;
	unsigned char const *src = (unsigned char const *)delta;
	unsigned int code;
	unsigned int count;
	unsigned char value;

	for (;;) {
		code = *src++;					// get delta source byte

		if (code == 0) {
			// SHORTRUN
			count = *src++;				// get count
			value = *src++;				// get XOR byte
			for (; count; count--) *dst++ ^= value;
			continue;
		}

		if ((code & 0x80) == 0) {
			// SHORTDUMP
			for (count = code; count; count--) *dst++ ^= *src++;
			continue;
		}

		// By now, we know it must be a LONGDUMP, SHORTSKIP, LONGRUN, or LONGSKIP
		code -= 0x80;
		if (code == 0) {
			code = src[0] | (src[1] << 8);	// get word code
			src += 2;
			if (code == 0) break;			// long count of zero means stop
			if (code & 0x8000) {
				code -= 0x8000;
				if (code & 0x4000) {
					// LONGRUN
					value = *src++;
					for (count = code - 0x4000; count; count--) *dst++ ^= value;
				} else {
					// LONGDUMP
					for (count = code; count; count--) *dst++ ^= *src++;
				}
				continue;
			}
		}

		// SHORTSKIP AND LONGSKIP
		dst += code;
	}
	return 0;		// eax is always zero when the stop code is reached
}


/*
;----------------------------------------------------------------------------

;***************************************************************************
;* APPLY_XOR_DELTA_To_Page_Or_Viewport -- Calls the copy or the XOR funtion.           *
;*                                                                         *
;*									   *
;* 	This funtion is call to either xor or copy XOR_Delta data onto a   *
;*	page instead of a buffer.  The routine will set up the registers   *
;*	need for the actual routines that will perform the copy or xor.	   *
;*									   *
;*	The registers are setup as follows :				   *
;*		es:edi - destination segment:offset onto page.		   *
;*		ds:esi - source buffer segment:offset of delta data.	   *
;*		dx,cx,ax - are all zeroed out before entry.		   *
;*                                                                         *
;* INPUT:                                                                  *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* WARNINGS:                                                               *
;*                                                                         *
;* HISTORY:                                                                *
;*   03/09/1992  SB : Created.                                             *
;*=========================================================================*
*/

void __cdecl Apply_XOR_Delta_To_Page_Or_Viewport(void *target, void *delta, int width, int nextrow, int copy)
{
	/*
	USES 	ebx,ecx,edx,edi,esi
	ARG	target:DWORD		; pointer to the destination buffer.
	ARG	delta:DWORD		; pointer to the delta buffer.
	ARG	width:DWORD		; width of animation.
	ARG	nextrow:DWORD		; Page/Buffer width - anim width.
	ARG	copy:DWORD		; should it be copied or xor'd?
	*/
	
	// webcandc: C++ port of the original x86 routine
	XORDelta_Target = (unsigned char *)target;		// Get the target pointer.
	XORDelta_Source = (unsigned char const *)delta;	// Get the delta pointer.
	XORDelta_Width = (unsigned int)width;				// max column for speed compares

	// Now call the correct function to either copy or xor the data.
	if (copy == DO_XOR) {
		XOR_Delta_Buffer(nextrow);
	} else {
		Copy_Delta_Buffer(nextrow);
	}
}


/*
;----------------------------------------------------------------------------


;***************************************************************************
;* XOR_DELTA_BUFFER -- Xor's the data in a XOR Delta format to a page.     *
;*	This will only work right if the page has the previous data on it. *
;*	This function should only be called by XOR_Delta_Buffer_To_Page_Or_Viewport.   *
;*      The registers must be setup as follows :                           *
;*                                                                         *
;* INPUT:                                                                  *
;*	es:edi - destination segment:offset onto page.		 	   *
;*	ds:esi - source buffer segment:offset of delta data.	 	   *
;*	edx,ecx,eax - are all zeroed out before entry.		 	   *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* WARNINGS:                                                               *
;*                                                                         *
;* HISTORY:                                                                *
;*   03/09/1992  SB : Created.                                             *
;*=========================================================================*
*/
void __cdecl XOR_Delta_Buffer(int nextrow)
{
	/*		  
	ARG	nextrow:DWORD
	*/
	
	// webcandc: C++ port of the original x86 routine
	WW_Delta_To_Page(nextrow, true);
}


/*
;----------------------------------------------------------------------------


;***************************************************************************
;* COPY_DELTA_BUFFER -- Copies XOR Delta Data to a section of a page.      *
;*	This function should only be called by XOR_Delta_Buffer_To_Page_Or_Viewport.   *
;*      The registers must be setup as follows :                           *
;*                                                                         *
;* INPUT:                                                                  *
;*	es:edi - destination segment:offset onto page.		 	   *
;*	ds:esi - source buffer segment:offset of delta data.	 	   *
;*	dx,cx,ax - are all zeroed out before entry.		 	   *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* WARNINGS:                                                               *
;*                                                                         *
;* HISTORY:                                                                *
;*   03/09/1992  SB : Created.                                             *
;*=========================================================================*
*/
void __cdecl Copy_Delta_Buffer(int nextrow)
{
	/*		  
	ARG	nextrow:DWORD
	*/
	
	// webcandc: C++ port of the original x86 routine
	WW_Delta_To_Page(nextrow, false);
}
/*
;----------------------------------------------------------------------------
*/






















/*
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D    S T U D I O S        **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Westwood Library                         *
;*                                                                         *
;*                    File Name : FADING.ASM                               *
;*                                                                         *
;*                   Programmer : Joe L. Bostic                            *
;*                                                                         *
;*                   Start Date : August 20, 1993                          *
;*                                                                         *
;*                  Last Update : August 20, 1993   [JLB]                  *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *

IDEAL
P386
MODEL USE32 FLAT

GLOBAL	C Build_Fading_Table	:NEAR

	CODESEG

;***********************************************************
; BUILD_FADING_TABLE
;
; void *Build_Fading_Table(void *palette, void *dest, long int color, long int frac);
;
; This routine will create the fading effect table used to coerce colors
; from toward a common value.  This table is used when Fading_Effect is
; active.
;
; Bounds Checking: None
;*
*/
void * __cdecl Build_Fading_Table(void const *palette, void const *dest, long int color, long int frac)
{
	/*
	PROC	Build_Fading_Table C near
	USES	ebx, ecx, edi, esi
	ARG	palette:DWORD
	ARG	dest:DWORD
	ARG	color:DWORD
	ARG	frac:DWORD

	LOCAL	matchvalue:DWORD	; Last recorded match value.
	LOCAL	targetred:BYTE		; Target gun red.
	LOCAL	targetgreen:BYTE	; Target gun green.
	LOCAL	targetblue:BYTE		; Target gun blue.
	LOCAL	idealred:BYTE
	LOCAL	idealgreen:BYTE
	LOCAL	idealblue:BYTE
	LOCAL	matchcolor:BYTE		; Tentative match color.
	*/
	
	int matchvalue = 0;	//:DWORD	; Last recorded match value.
	unsigned char targetred = 0;		//BYTE		; Target gun red.
	unsigned char targetgreen = 0;	//BYTE		; Target gun green.
	unsigned char targetblue = 0;		//BYTE		; Target gun blue.
	unsigned char idealred = 0;		//BYTE	
	unsigned char idealgreen = 0;		//BYTE	
	unsigned char idealblue = 0;		//BYTE	
	unsigned char matchcolor = 0;		//:BYTE		; Tentative match color.
	
	// webcandc: C++ port of the original x86 routine
	// If the source palette is NULL, then just return with current fading table pointer.
	if (palette == 0 || dest == 0) return (void *)dest;

	// Fractions above 255 become 255.
	if ((unsigned long)frac >= 0x100) frac = 0xFF;

	unsigned char const *pal = (unsigned char const *)palette;
	unsigned char *table = (unsigned char *)dest;

	// Record the target gun values.
	unsigned char const *gun = pal + WW_Mul((int)color, 3);
	targetred = gun[0];
	targetgreen = gun[1];
	targetblue = gun[2];

	// Transparent black never gets remapped.
	*table++ = 0;

	unsigned char fraction = (unsigned char)((unsigned long)frac >> 1);

	// index = source palette logical number (1..255).
	for (unsigned int index = 1; index <= 255; index++) {
		gun = pal + index * 3;

		// new = orig - ((orig-target) * fraction);
		idealred = WW_Fade_Gun(gun[0], targetred, fraction);
		idealgreen = WW_Fade_Gun(gun[1], targetgreen, fraction);
		idealblue = WW_Fade_Gun(gun[2], targetblue, fraction);

		// Sweep through the entire existing palette to find the closest
		// matching color.  Never matches with color 0.
		matchcolor = (unsigned char)color;		// Default color (self).
		matchvalue = -1;						// Ridiculous match value init.
		for (unsigned int c = 1; c <= 255; c++) {
			// Recursion through the fading table won't work if a color is allowed
			// to remap to itself.  Prevent this from occuring.
			if (c == index) continue;

			// Build the comparison value based on the sum of the differences of the color
			// guns squared.
			unsigned char const *p = pal + c * 3;
			unsigned int value = WW_Gun_Distance(p[0], idealred)
									 + WW_Gun_Distance(p[1], idealgreen)
									 + WW_Gun_Distance(p[2], idealblue);
			if (value == 0) {					// If perfect match found then quit early.
				matchcolor = (unsigned char)c;
				break;
			}
			if (value <= (unsigned int)matchvalue) {
				matchvalue = (int)value;		// Record new possible color.
				matchcolor = (unsigned char)c;
			}
		}

		// When the loop exits, we have found the closest match.
		*table++ = matchcolor;
	}
	return (void *)dest;
}
























/*
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Westwood Library                         *
;*                                                                         *
;*                    File Name : PAL.ASM                                  *
;*                                                                         *
;*                   Programmer : Joe L. Bostic                            *
;*                                                                         *
;*                   Start Date : May 30, 1992                             *
;*                                                                         *
;*                  Last Update : April 27, 1994   [BR]                    *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;*   Set_Palette_Range -- Sets changed values in the palette.              *
;*   Bump_Color -- adjusts specified color in specified palette            *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *
;********************** Model & Processor Directives ************************
IDEAL
P386
MODEL USE32 FLAT


;include "keyboard.inc"
FALSE = 0
TRUE  = 1

;****************************** Declarations ********************************
GLOBAL 		C Set_Palette_Range:NEAR
GLOBAL 		C Bump_Color:NEAR
GLOBAL  	C CurrentPalette:BYTE:768
GLOBAL		C PaletteTable:byte:1024


;********************************** Data ************************************
LOCALS ??

	DATASEG

CurrentPalette	DB	768 DUP(255)	; copy of current values of DAC regs
PaletteTable	DB	1024 DUP(0)

IFNDEF LIB_EXTERNS_RESOLVED
VertBlank	DW	0		; !!!! this should go away
ENDIF


;********************************** Code ************************************
	CODESEG
*/

extern "C" unsigned char CurrentPalette[768] = {255};	//	DB	768 DUP(255)	; copy of current values of DAC regs
extern "C" unsigned char PaletteTable[1024] = {0};		//	DB	1024 DUP(0)


/*
;***************************************************************************
;* SET_PALETTE_RANGE -- Sets a palette range to the new pal                *
;*                                                                         *
;* INPUT:                                                                  *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* PROTO:                                                                  *
;*                                                                         *
;* WARNINGS:	This routine is optimized for changing a small number of   *
;*		colors in the palette.
;*                                                                         *
;* HISTORY:                                                                *
;*   03/07/1995 PWG : Created.                                             *
;*=========================================================================*
*/
void __cdecl Set_Palette_Range(void *palette)
{
	memcpy(CurrentPalette, palette, 768);
	Set_DD_Palette(palette);

	/*
	PROC	Set_Palette_Range C NEAR
	ARG	palette:DWORD

	GLOBAL	Set_DD_Palette_:near
	GLOBAL	Wait_Vert_Blank_:near
	
	pushad
	mov	esi,[palette]
	mov	ecx,768/4
	mov	edi,offset CurrentPalette
	cld
	rep	movsd
	;call	Wait_Vert_Blank_
	mov	eax,[palette]
	push	eax
	call	Set_DD_Palette_
	pop	eax
	popad
	ret
	*/
}


/*
;***************************************************************************
;* Bump_Color -- adjusts specified color in specified palette              *
;*                                                                         *
;* INPUT:                                                                  *
;*	VOID *palette	- palette to modify				   *
;*	WORD changable	- color # to change				   *
;*	WORD target	- color to bend toward				   *
;*                                                                         *
;* OUTPUT:                                                                 *
;*                                                                         *
;* WARNINGS:                                                               *
;*                                                                         *
;* HISTORY:                                                                *
;*   04/27/1994 BR : Converted to 32-bit.                                  *
;*=========================================================================*
; BOOL cdecl Bump_Color(VOID *palette, WORD changable, WORD target);
*/ 
BOOL __cdecl Bump_Color(void *pal, int color, int desired)
{
	/*		  
PROC Bump_Color C NEAR
	USES ebx,ecx,edi,esi
	ARG	pal:DWORD, color:WORD, desired:WORD
	LOCAL	changed:WORD		; Has palette changed?
	 */ 
	
	short short_color = (short) color;
	short short_desired = (short) desired;
	bool changed = false;
	
	// webcandc: C++ port of the original x86 routine
	unsigned char *changable = (unsigned char *)pal + 3u * (unsigned short)short_color;			// Offset to changable color.
	unsigned char const *target = (unsigned char const *)pal + 3u * (unsigned short)short_desired;	// Offset to target color.

	changed = false;			// Presume no change.
	for (int gun = 0; gun < 3; gun++) {		// Three color guns.
		if (target[gun] != changable[gun]) {
			changed = true;
			if (target[gun] < changable[gun]) {
				changable[gun]--;
			} else {
				changable[gun]++;
			}
		}
	}
	return changed ? TRUE : FALSE;
}















/*
;***************************************************************************
;**     C O N F I D E N T I A L --- W E S T W O O D   S T U D I O S       **
;***************************************************************************
;*                                                                         *
;*                 Project Name : GraphicViewPortClass			   *
;*                                                                         *
;*                    File Name : PUTPIXEL.ASM                             *
;*                                                                         *
;*                   Programmer : Phil Gorrow				   *
;*                                                                         *
;*                   Start Date : June 7, 1994				   *
;*                                                                         *
;*                  Last Update : June 8, 1994   [PWG]                     *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;*   VVPC::Put_Pixel -- Puts a pixel on a virtual viewport                 *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *

IDEAL
P386
MODEL USE32 FLAT

INCLUDE ".\drawbuff.inc"
INCLUDE ".\gbuffer.inc"


CODESEG
*/

/*
;***************************************************************************
;* VVPC::PUT_PIXEL -- Puts a pixel on a virtual viewport                   *
;*                                                                         *
;* INPUT:	WORD the x position for the pixel relative to the upper    *
;*			left corner of the viewport			   *
;*		WORD the y pos for the pixel relative to the upper left	   *
;*			corner of the viewport				   *
;*		UBYTE the color of the pixel to write			   *
;*                                                                         *
;* OUTPUT:      none                                                       *
;*                                                                         *
;* WARNING:	If pixel is to be placed outside of the viewport then	   *
;*		this routine will abort.				   *
;*									   *
;* HISTORY:                                                                *
;*   06/08/1994 PWG : Created.                                             *
;*=========================================================================*
	PROC	Buffer_Put_Pixel C near
	USES	eax,ebx,ecx,edx,edi
*/

void __cdecl Buffer_Put_Pixel(void * this_object, int x_pixel, int y_pixel, unsigned char color)
{
			  
	/*
	ARG    	this_object:DWORD				; this is a member function
	ARG	x_pixel:DWORD				; x position of pixel to set
	ARG	y_pixel:DWORD				; y position of pixel to set
	ARG    	color:BYTE				; what color should we clear to
	*/
	
	// webcandc: C++ port of the original x86 routine
	// Verify that the X and Y pixel offsets are legal
	if ((unsigned)x_pixel >= (unsigned)GVP_Width(this_object)) return;
	if ((unsigned)y_pixel >= (unsigned)GVP_Height(this_object)) return;

	// Write the pixel to the screen
	*(GVP_Base(this_object) + x_pixel + WW_Mul(y_pixel, GVP_Stride(this_object))) = color;
}








/*
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Support Library                          *
;*                                                                         *
;*                    File Name : cliprect.asm                             *
;*                                                                         *
;*                   Programmer : Julio R Jerez                            *
;*                                                                         *
;*                   Start Date : Mar, 2 1995                              *
;*                                                                         *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;* int Clip_Rect ( int * x , int * y , int * dw , int * dh , 		   *
;*	       	   int width , int height ) ;          			   *
;* int Confine_Rect ( int * x , int * y , int * dw , int * dh , 	   *
;*	       	      int width , int height ) ;          		   *
;*									   *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *


IDEAL
P386
MODEL USE32 FLAT

GLOBAL	 C Clip_Rect	:NEAR
GLOBAL	 C Confine_Rect	:NEAR

CODESEG

;***************************************************************************
;* Clip_Rect -- clip a given rectangle against a given window		   *
;*                                                                         *
;* INPUT:   &x , &y , &w , &h  -> Pointer to rectangle being clipped       *
;*          width , height     -> dimension of clipping window             *
;*                                                                         *
;* OUTPUT: a) Zero if the rectangle is totally contained by the 	   *
;*	      clipping window.						   *
;*	   b) A negative value if the rectangle is totally outside the     *
;*            the clipping window					   *
;*	   c) A positive value if the rectangle	was clipped against the	   *
;*	      clipping window, also the values pointed by x, y, w, h will  *
;*	      be modified to new clipped values	 			   *
;*									   *
;*   05/03/1995 JRJ : added comment                                        *
;*=========================================================================*
; int Clip_Rect (int* x, int* y, int* dw, int* dh, int width, int height);          			   *
*/

extern "C" int __cdecl Clip_Rect ( int * x , int * y , int * w , int * h , int width , int height )
{		

/*
	PROC	Clip_Rect C near
	uses	ebx,ecx,edx,esi,edi
	arg	x:dword
	arg	y:dword
	arg	w:dword
	arg	h:dword
	arg	width:dword
	arg	height:dword
*/

	// webcandc: C++ port of the original x86 routine
	// This Clipping algorithm is a derivation of the very well known
	// Cohen-Sutherland Line-Clipping test. Due to its simplicity and efficiency
	// it is probably the most commontly implemented algorithm both in software
	// and hardware for clipping lines, rectangles, and convex polygons against
	// a rectagular clipping window. For reference see
	// "COMPUTER GRAPHICS principles and practice by Foley, Vandam, Feiner, Hughes
	// pages 113 to 177".
	int x0 = *x;
	int y0 = *y;
	int x1 = WW_Add(*w, x0);		// x1 = x0 + dw
	int y1 = WW_Add(*h, y0);		// y1 = y0 + dh
	unsigned code0 = WW_Clip_Code(x0, y0, width, height);
	unsigned code1 = WW_Clip_Code(x1, y1, width, height);

	// now perform the rejection test
	if (code0 & code1) return -1;
	// now perform the aceptance test
	if ((code0 | code1) == 0) return 0;

	// we need to clip the rectangle iteratively
	if (code0 & 8) {
		// spill out the left edge of the window
		int old_x = *x;
		*x = 0;
		*w = WW_Add(*w, old_x);
	}
	if (code0 & 2) {
		// spill out the bottom edge of the window
		int old_y = *y;
		*y = 0;
		*h = WW_Add(*h, old_y);
	}
	if (code1 & 4) {
		// spill out the right edge of the window
		int cur_x = *x;
		*w = WW_Sub(width, cur_x);
		if (width <= cur_x) return -1;		// clipped retangle has no width
	}
	if (code1 & 1) {
		// spill out the top edge of the window
		int cur_y = *y;
		*h = WW_Sub(height, cur_y);
		if (height <= cur_y) return -1;
	}
	return 1;		// signal the calling program that the rectangle was modify

	//ENDP	Clip_Rect
}

/*
;***************************************************************************
;* Confine_Rect -- clip a given rectangle against a given window	   *
;*                                                                         *
;* INPUT:   &x,&y,w,h    -> Pointer to rectangle being clipped       *
;*          width,height     -> dimension of clipping window             *
;*                                                                         *
;* OUTPUT: a) Zero if the rectangle is totally contained by the 	   *
;*	      clipping window.						   *
;*	   c) A positive value if the rectangle	was shifted in position    *
;*	      to fix inside the clipping window, also the values pointed   *
;*	      by x, y, will adjusted to a new values	 		   *
;*									   *
;*  NOTE:  this function make not attempt to verify if the rectangle is	   *
;*	   bigger than the clipping window and at the same time wrap around*
;*	   it. If that is the case the result is meaningless		   *
;*=========================================================================*
; int Confine_Rect (int* x, int* y, int dw, int dh, int width, int height);          			   *
*/

extern "C" int __cdecl Confine_Rect ( int * x , int * y , int w , int h , int width , int height )
{
	
/*
	PROC	Confine_Rect C near
	uses	ebx, esi,edi
	arg	x:dword
	arg	y:dword
	arg	w:dword
	arg	h:dword
	arg	width :dword
	arg	height:dword
*/
	// webcandc: C++ port of the original x86 routine
	int result = 0;
	int neg;
	int over;

	neg = WW_Neg(*x);								// -x
	over = WW_Sub(WW_Sub(WW_Add(w, *x), width), 1);	// x + w - width - 1
	if (!((neg & over) < 0)) {
		result = 1;
		if (neg < 0) {
			*x = WW_Sub(*x, WW_Add(over, 1));		// shift_right
		} else {
			*x = 0;
		}
	}

	neg = WW_Neg(*y);
	over = WW_Sub(WW_Sub(WW_Add(h, *y), height), 1);
	if (!((neg & over) < 0)) {
		result = 1;
		if (neg < 0) {
			*y = WW_Sub(*y, WW_Add(over, 1));		// shift_top
		} else {
			*y = 0;
		}
	}
	return result;
}









/*
; $Header: //depot/Projects/Mobius/QA/Project/Run/SOURCECODE/TIBERIANDAWN/WIN32LIB/DrawMisc.cpp#139 $
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Library routine                          *
;*                                                                         *
;*                    File Name : UNCOMP.ASM                               *
;*                                                                         *
;*                   Programmer : Christopher Yates                        *
;*                                                                         *
;*                  Last Update : 20 August, 1990   [CY]                   *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;*                                                                         *
; ULONG LCW_Uncompress(BYTE *source, BYTE *dest, ULONG length);		   *
;*                                                                         *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *

IDEAL
P386
MODEL USE32 FLAT

GLOBAL            C LCW_Uncompress          :NEAR

CODESEG

; ----------------------------------------------------------------
;
; Here are prototypes for the routines defined within this module:
;
; ULONG LCW_Uncompress(BYTE *source, BYTE *dest, ULONG length);
;
; ----------------------------------------------------------------
*/

extern "C" unsigned long __cdecl LCW_Uncompress(void *source, void *dest, unsigned long length_)
{
//PROC	LCW_Uncompress C near
//
//	USES ebx,ecx,edx,edi,esi
//
//	ARG	source:DWORD
//	ARG	dest:DWORD
//	ARG	length:DWORD
//;LOCALS
//	LOCAL a1stdest:DWORD
//	LOCAL maxlen:DWORD
//	LOCAL lastbyte:DWORD
//	LOCAL lastcom:DWORD
//	LOCAL lastcom1:DWORD
		
	// webcandc: C++ port of the original x86 routine
	//
	// uncompress data to the following codes in the format b = byte, w = word
	// n = byte code pulled from compressed data
	//   Bit field of n		command		description
	// n=0xxxyyyy,yyyyyyyy		short run	back y bytes and run x+3
	// n=10xxxxxx,n1,n2,...,nx+1	med length	copy the next x+1 bytes
	// n=11xxxxxx,w1			med run		run x+3 bytes from offset w1
	// n=11111111,w1,w2		long copy	copy w1 bytes from offset w2
	// n=11111110,w1,b1		long run	run byte b1 for w1 bytes
	// n=10000000			end		end of data reached
	//
	// All copies are forward byte copies (the source may overlap the
	// destination to replicate a pattern); the asm's dword paths produce
	// the same bytes.
	unsigned char *dst = (unsigned char *)dest;
	unsigned char *a1stdest = dst;
	unsigned char *lastbyte = dst + length_;
	unsigned char const *source_ptr = (unsigned char const *)source;	// ebx: saved source offset
	unsigned char const *src;
	unsigned int maxlen;
	unsigned int count;
	unsigned int code;

	for (;;) {
		maxlen = (unsigned int)(lastbyte - dst);		// get the remaining byte to uncomp
		if (maxlen == 0) break;							// were done

		src = source_ptr;
		code = *src++;

		if ((code & 0x80) == 0) {
			// short run: back y bytes and run x+3
			count = (code >> 4) + 3;
			if (count > maxlen) count = maxlen;			// max it out so it dosen't overrun
			unsigned int offset = ((code & 0x0F) << 8) | src[0];
			source_ptr = src + 1;
			WW_Copy_Forward(dst, dst - offset, (int)count);
			dst += count;
			continue;
		}

		if ((code & 0x40) == 0) {
			if (code == 0x80) break;						// is it the end?
			// med length: copy the next x+1 bytes
			count = code & 0x3F;
			if (count > maxlen) count = maxlen;
			WW_Copy_Forward(dst, src, (int)count);
			dst += count;
			source_ptr = src + count;
			continue;
		}

		count = (code & 0x3F) + 3;

		if (code == 0xFE) {
			// long run: run byte b1 for w1 bytes
			count = src[0] | (src[1] << 8);
			unsigned char value = src[2];
			source_ptr = src + 3;
			if (count > maxlen) count = maxlen;
			memset(dst, value, count);
			dst += count;
			continue;
		}

		if (code == 0xFF) {
			// long copy: copy w1 bytes from offset w2
			count = src[0] | (src[1] << 8);
			src += 2;
		}

		// med run / long copy from an absolute offset in the destination
		unsigned char const *from = a1stdest + (src[0] | (src[1] << 8));
		source_ptr = src + 2;
		if (count > maxlen) count = maxlen;
		WW_Copy_Forward(dst, from, (int)count);
		dst += count;
	}

	return (unsigned long)(dst - (unsigned char *)dest);
}














/*
;***************************************************************************
;**   C O N F I D E N T I A L --- W E S T W O O D   A S S O C I A T E S   **
;***************************************************************************
;*                                                                         *
;*                 Project Name : Westwood 32 bit Library                  *
;*                                                                         *
;*                    File Name : TOPAGE.ASM                               *
;*                                                                         *
;*                   Programmer : Phil W. Gorrow                           *
;*                                                                         *
;*                   Start Date : June 8, 1994                             *
;*                                                                         *
;*                  Last Update : June 15, 1994   [PWG]                    *
;*                                                                         *
;*-------------------------------------------------------------------------*
;* Functions:                                                              *
;*   Buffer_To_Page -- Copies a linear buffer to a virtual viewport	   *
;* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - *

IDEAL
P386
MODEL USE32 FLAT

TRANSP	equ  0


INCLUDE ".\drawbuff.inc"
INCLUDE ".\gbuffer.inc"

CODESEG

;***************************************************************************
;* VVC::TOPAGE -- Copies a linear buffer to a virtual viewport		   *
;*                                                                         *
;* INPUT:	WORD	x_pixel		- x pixel on viewport to copy from *
;*		WORD	y_pixel 	- y pixel on viewport to copy from *
;*		WORD	pixel_width	- the width of copy region	   *
;*		WORD	pixel_height	- the height of copy region	   *
;*		BYTE *	src		- buffer to copy from		   *
;*		VVPC *  dest		- virtual viewport to copy to	   *
;*                                                                         *
;* OUTPUT:      none                                                       *
;*                                                                         *
;* WARNINGS:    Coordinates and dimensions will be adjusted if they exceed *
;*	        the boundaries.  In the event that no adjustment is 	   *
;*	        possible this routine will abort.  If the size of the 	   *
;*		region to copy exceeds the size passed in for the buffer   *
;*		the routine will automatically abort.			   *
;*									   *
;* HISTORY:                                                                *
;*   06/15/1994 PWG : Created.                                             *
;*=========================================================================*
 */ 

extern "C" long __cdecl Buffer_To_Page(int x_pixel, int y_pixel, int pixel_width, int pixel_height, void *src, void *dest)
{

/*
	PROC	Buffer_To_Page C near
	USES	eax,ebx,ecx,edx,esi,edi

	;*===================================================================
	;* define the arguements that our function takes.
	;*===================================================================
	ARG	x_pixel     :DWORD		; x pixel position in source
	ARG	y_pixel     :DWORD		; y pixel position in source
	ARG	pixel_width :DWORD		; width of rectangle to blit
	ARG	pixel_height:DWORD		; height of rectangle to blit
	ARG    	src         :DWORD		; this is a member function
	ARG	dest        :DWORD		; what are we blitting to

;	ARG	trans       :DWORD			; do we deal with transparents?

	;*===================================================================
	; Define some locals so that we can handle things quickly
	;*===================================================================
	LOCAL 	x1_pixel :dword
	LOCAL	y1_pixel :dword
	local	scr_x 	: dword
	local	scr_y 	: dword
	LOCAL	dest_ajust_width:DWORD
	LOCAL	scr_ajust_width:DWORD
	LOCAL	dest_area   :  dword
*/

	// webcandc: C++ port of the original x86 routine
	int x1_pixel;
	int y1_pixel;
	int scr_x;
	int scr_y;
	int dest_ajust_width;
	int scr_ajust_width;
	unsigned code0;
	unsigned code1;

	if (src == 0) return 0;		// (the asm returned whatever was in eax)

	// Clip dest Rectangle against source Window boundaries.
	scr_x = 0;
	scr_y = 0;
	int win_w = GVP_Width(dest);
	int win_h = GVP_Height(dest);
	x1_pixel = WW_Add(x_pixel, pixel_width);
	y1_pixel = WW_Add(y_pixel, pixel_height);
	code0 = WW_Clip_Code(x_pixel, y_pixel, win_w, win_h);
	code1 = WW_Clip_Code(x1_pixel, y1_pixel, win_w, win_h);
	if (code0 & code1) {
		// eax held y_pixel-(height+1) with the first clip code moved into al
		return (long)(int)(((unsigned)WW_Sub(WW_Sub(y_pixel, win_h), 1) & ~0xFFu) | code0);
	}
	if (code0 | code1) {
		if (code0 & 8) {
			scr_x = WW_Neg(x_pixel);
			x_pixel = 0;
		}
		if (code0 & 2) {
			scr_y = WW_Neg(y_pixel);
			y_pixel = 0;
		}
		if (code1 & 4) x1_pixel = win_w;
		if (code1 & 1) y1_pixel = win_h;
	}

	// do_blit:
	int stride = GVP_Stride(dest);
	unsigned char *dst = GVP_Base(dest) + WW_Mul(stride, y_pixel) + x_pixel;
	dest_ajust_width = WW_Sub(WW_Add(stride, x_pixel), x1_pixel);

	unsigned char const *src_ptr = (unsigned char const *)src;
	scr_ajust_width = WW_Add(WW_Sub(pixel_width, x1_pixel), x_pixel);
	src_ptr += WW_Add(WW_Mul(scr_y, pixel_width), scr_x);

	if (y1_pixel <= y_pixel) return x1_pixel;
	int height = WW_Sub(y1_pixel, y_pixel);
	int width = WW_Sub(x1_pixel, x_pixel);
	if (x1_pixel <= x_pixel) return width;

	// Forward bitblit only
	do {
		WW_Copy_Forward(dst, src_ptr, width);
		src_ptr += width;
		dst += width;
		src_ptr += scr_ajust_width;
		dst += dest_ajust_width;
	} while (--height != 0);

	return width;		// eax still held the clipped width
}

			//ENDP	Buffer_To_Page
		//END









/*

;***************************************************************************
;* VVPC::GET_PIXEL -- Gets a pixel from the current view port		   *
;*                                                                         *
;* INPUT:	WORD the x pixel on the screen.				   *
;*		WORD the y pixel on the screen.				   *
;*                                                                         *
;* OUTPUT:      UBYTE the pixel at the specified location		   *
;*                                                                         *
;* WARNING:	If pixel is to be placed outside of the viewport then	   *
;*		this routine will abort.				   *
;*                                                                         *
;* HISTORY:                                                                *
;*   06/07/1994 PWG : Created.                                             *
;*=========================================================================*
	PROC	Buffer_Get_Pixel C near
	USES	ebx,ecx,edx,edi

	ARG    	this_object:DWORD				; this is a member function
	ARG	x_pixel:DWORD				; x position of pixel to set
	ARG	y_pixel:DWORD				; y position of pixel to set
*/

extern "C" int __cdecl Buffer_Get_Pixel(void * this_object, int x_pixel, int y_pixel)
{
	// webcandc: C++ port of the original x86 routine
	// Verify that the X and Y pixel offsets are legal.  (Like the asm, an
	// out-of-range coordinate is returned as-is, since it was left in eax.)
	if ((unsigned)x_pixel >= (unsigned)GVP_Width(this_object)) return x_pixel;
	if ((unsigned)y_pixel >= (unsigned)GVP_Height(this_object)) return y_pixel;

	// Read the pixel from the screen
	return *(GVP_Base(this_object) + x_pixel + WW_Mul(y_pixel, GVP_Stride(this_object)));
}


