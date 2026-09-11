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
**	webcandc: WINASM.CPP -- C++ port of the non-modem parts of WINASM.ASM, plus stubs for
**	the hardware-only routines in WINASM.ASM, MMX.ASM and PAGFAULT.ASM.
**
**	- Asm_Interpolate, Asm_Interpolate_Line_Double, Asm_Interpolate_Line_Interpolate:
**	  2x stretch of a 320x200 image through PaletteInterpolationTable (INTERPAL.CPP).
**	- Asm_Create_Palette_Interpolation_Table: builds that table from InterpolationPalette.
**	- ModeX_Blit, Set_Palette_Register, Stop_Execution, Detect_MMX_Availability, Init_MMX,
**	  Install_Page_Fault_Handle: no-ops (VGA ports, CPU detection, DPMI).
**
**	The Greenleaf/modem entry points and the modem data (_AbortModemFunctionPtr, MouseQX,
**	MouseQY) are not ported. Memory_Error_Exit is not defined here because ALLOC.CPP
**	already defines it.
**
**	References: reference/Vanilla-Conquer/common/winasm.cpp and interpal.cpp. The asm is the
**	spec; the one deliberate difference is marked "webcandc:".
*/

#include	"function.h"


/*
**	Line buffers (WINASM.ASM DATASEG: 640 dwords each).
*/
static unsigned char LineBuffer[640 * 4];
static unsigned char TopLine[640 * 4];
static unsigned char BottomLine[640 * 4];


/*
**	Table lookup as the asm does it: the two pixels are loaded as one little-endian word,
**	so the byte offset is first | (second << 8), i.e. PaletteInterpolationTable[second][first].
*/
static inline unsigned char Interpolated(unsigned char first, unsigned char second)
{
	return((&PaletteInterpolationTable[0][0])[first | (second << 8)]);
}


/***************************************************************************
 * Interpolate_Single_Line -- Stretch one source line 2x horizontally.     *
 *                                                                         *
 *    Each source pixel p[i] is followed by the blend of p[i] and p[i+1];  *
 *    the last one is followed by a 0. Processes (width-2)/2 pixel pairs    *
 *    then the final pair, exactly like the asm, and returns the source    *
 *    position after it (for odd widths that is one pixel short of the     *
 *    next line, as in the original).                                      *
 *    If copy is not NULL the output is written there too.                 *
 *=========================================================================*/
static unsigned char const * Interpolate_Single_Line(unsigned char const * src, int source_width, unsigned char * dest, unsigned char * copy)
{
	/*
	**	webcandc: the asm loop is "dec ecx / jnz", so widths below 4 would run away.
	*/
	int pairs = (source_width - 2) >> 1;

	for (int count = 0; count < pairs; count++) {
		unsigned char p0 = src[0];
		unsigned char p1 = src[1];
		unsigned char p2 = src[2];

		dest[0] = p0;
		dest[1] = Interpolated(p0, p1);
		dest[2] = p1;
		dest[3] = Interpolated(p1, p2);
		if (copy != NULL) {
			memcpy(copy, dest, 4);
			copy += 4;
		}
		src += 2;
		dest += 4;
	}

	/*
	**	Last three pixels and a blank on the end of the row.
	*/
	unsigned char q0 = src[0];
	unsigned char q1 = src[1];
	dest[0] = q0;
	dest[1] = Interpolated(q0, q1);
	dest[2] = q1;
	dest[3] = 0;
	if (copy != NULL) {
		memcpy(copy, dest, 4);
	}
	return(src + 2);
}


/***************************************************************************
 * Interpolate_Between_Lines -- Blend two stretched lines pixel by pixel.  *
 *=========================================================================*/
static void Interpolate_Between_Lines(unsigned char const * source1, unsigned char const * source2, unsigned char * destination, int source_width)
{
	for (int count = source_width * 2; count > 0; count--) {
		*destination++ = Interpolated(*source1++, *source2++);
	}
}


/*********************************************************************************************
 * Asm_Interpolate -- interpolate a 320x200 buffer to a 640x400 screen                       *
 *                                                                                           *
 *    Stretches each line horizontally; dest_width is the byte step between output lines     *
 *    (INTERPAL passes two screen rows, leaving every other row untouched).                  *
 *                                                                                           *
 * HISTORY:                                                                                  *
 *   12/15/95 ST : Created.                                                                  *
 *===========================================================================================*/
extern "C" void __cdecl Asm_Interpolate(unsigned char* src_ptr, unsigned char* dest_ptr, int lines, int src_width, int dest_width)
{
	unsigned char const * src = src_ptr;
	unsigned char * dest = dest_ptr;

	for (; lines > 0; lines--) {
		src = Interpolate_Single_Line(src, src_width, dest, NULL);
		dest += dest_width;
	}
}


/*********************************************************************************************
 * Asm_Interpolate_Line_Double -- Stretch horizontally and duplicate each line.              *
 *                                                                                           *
 *    dest_width is the byte step for a pair of output lines.                                *
 *===========================================================================================*/
extern "C" void __cdecl Asm_Interpolate_Line_Double(unsigned char* src_ptr, unsigned char* dest_ptr, int lines, int src_width, int dest_width)
{
	unsigned char const * src = src_ptr;
	unsigned char * dest = dest_ptr;
	unsigned half = (unsigned)dest_width >> 1;
	unsigned bytes = ((unsigned)src_width >> 1) * 4;

	for (; lines > 0; lines--) {
		src = Interpolate_Single_Line(src, src_width, dest, LineBuffer);
		dest += half;
		memcpy(dest, LineBuffer, bytes);
		dest += half;
	}
}


/*********************************************************************************************
 * Asm_Interpolate_Line_Interpolate -- Stretch horizontally and blend in-between lines.      *
 *                                                                                           *
 *    Output line 2n is source line n stretched; line 2n+1 blends lines n and n+1.           *
 *    dest_width is the byte step for a pair of output lines.                                *
 *                                                                                           *
 *    webcandc: for the final output line the asm stretches the source line *after* the last *
 *    one (reading past the source buffer). The last real line is repeated instead.          *
 *===========================================================================================*/
extern "C" void __cdecl Asm_Interpolate_Line_Interpolate(unsigned char* src_ptr, unsigned char* dest_ptr, int lines, int src_width, int dest_width)
{
	unsigned char const * src = src_ptr;
	unsigned char * dest = dest_ptr;
	unsigned char * next_line = TopLine;
	unsigned char * last_line = BottomLine;
	unsigned half = (unsigned)dest_width >> 1;
	unsigned bytes = ((unsigned)src_width >> 1) * 4;

	if (lines <= 0) return;

	Interpolate_Single_Line(src, src_width, next_line, NULL);
	unsigned char * temp = next_line;
	next_line = last_line;
	last_line = temp;
	src += src_width;
	lines--;

	for (; lines > 0; lines--) {
		Interpolate_Single_Line(src, src_width, next_line, NULL);
		Interpolate_Between_Lines(last_line, next_line, LineBuffer, src_width);

		memcpy(dest, last_line, bytes);
		dest += half;
		memcpy(dest, LineBuffer, bytes);
		dest += half;
		src += src_width;

		temp = next_line;
		next_line = last_line;
		last_line = temp;
	}

	memcpy(dest, last_line, bytes);
}


/*********************************************************************************************
 * Asm_Create_Palette_Interpolation_Table -- Build the colour blend table.                  *
 *                                                                                           *
 *    PaletteInterpolationTable[i][j] = palette entry closest to the average of colours i    *
 *    and j of InterpolationPalette (sum of squared 8 bit gun differences; on a tie the      *
 *    later colour wins, an exact match stops the search).                                   *
 *===========================================================================================*/
extern "C" void __cdecl Asm_Create_Palette_Interpolation_Table(void)
{
	unsigned char const * palette = InterpolationPalette;
	unsigned char * dest = &PaletteInterpolationTable[0][0];

	for (int first = 0; first < 256; first++) {
		unsigned char const * first_color = palette + first * 3;

		for (int second = 0; second < 256; second++) {
			unsigned char const * second_color = palette + second * 3;

			/*
			**	"add bl,[edi] / shr bl,1" -- 8 bit sum, then halved.
			*/
			unsigned char red = (unsigned char)(first_color[0] + second_color[0]) >> 1;
			unsigned char green = (unsigned char)(first_color[1] + second_color[1]) >> 1;
			unsigned char blue = (unsigned char)(first_color[2] + second_color[2]) >> 1;

			unsigned int closest_colour = 0;
			unsigned int distance_of_closest = 0xFFFFFFFFU;
			unsigned char const * match = palette;

			for (int count = 0; count < 256; count++, match += 3) {
				signed char dr = (signed char)(unsigned char)(match[0] - red);
				signed char dg = (signed char)(unsigned char)(match[1] - green);
				signed char db = (signed char)(unsigned char)(match[2] - blue);
				unsigned int distance = (unsigned int)(dr * dr) + (unsigned int)(dg * dg) + (unsigned int)(db * db);

				if (distance <= distance_of_closest) {
					distance_of_closest = distance;
					closest_colour = count;
					if (distance == 0) break;
				}
			}
			*dest++ = (unsigned char)closest_colour;
		}
	}
}


/*
**	Hardware-only routines: nothing to do here.
*/
extern "C" void ModeX_Blit(GraphicBufferClass *)
{
}


/*
**	Set_Palette_Register_ was a Watcom register-call routine (AL = register, DL/BL/CL = red/
**	green/blue) that programmed the VGA DAC. Nothing declares or calls it.
*/
extern "C" void Set_Palette_Register(int, int, int, int)
{
}


extern "C" void Stop_Execution(void)
{
}


extern "C" int Detect_MMX_Availability(void)
{
	return(0);
}


extern "C" void Init_MMX(void)
{
}


extern "C" void Install_Page_Fault_Handle(void)
{
}
