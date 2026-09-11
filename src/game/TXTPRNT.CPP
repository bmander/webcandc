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
**	webcandc: TXTPRNT.CPP -- C++ port of TXTPRNT.ASM.
**
**	Buffer_Print draws a string in the current 4 bit font (FontPtr) into a graphic
**	viewport; Get_Font_Palette_Ptr returns the ColorXlat colour translation table, which
**	this file also defines (it lived in TXTPRNT.ASM's data segment).
**
**	Reference: reference/Vanilla-Conquer/common/font.cpp (Buffer_Print). The asm is the
**	spec: this follows its return value (next draw position, 0 on overflow), its <CR>/<LF>
**	and wrap behaviour, and its high nibble lookup (ColorXlat[nibble << 4]).
*/

#include	"function.h"


/*
**	Offsets of the font header fields.
*/
#define	FONTINFOBLOCK		4
#define	FONTOFFSETBLOCK		6
#define	FONTWIDTHBLOCK		8
#define	FONTDATABLOCK		10
#define	FONTHEIGHTBLOCK		12

#define	FONTINFOMAXHEIGHT	4
#define	FONTINFOMAXWIDTH	5


/*
**	Colour translation table. A font pixel's low nibble n is looked up at ColorXlat[0][n]
**	and its high nibble at ColorXlat[n][0], so both the first row and the first column hold
**	the 16 font colours (Set_Font_Palette_Range updates both).
**	webcandc: the asm table was 241 bytes (ended at [15][0]); the zero-padded 16x16 version
**	lets callers that save and restore 256 bytes of it (CONQUER.CPP) stay in bounds.
*/
extern "C" {
unsigned char ColorXlat[16][16] = {
	{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F},
	{0x01}, {0x02}, {0x03}, {0x04}, {0x05}, {0x06}, {0x07},
	{0x08}, {0x09}, {0x0A}, {0x0B}, {0x0C}, {0x0D}, {0x0E}, {0x0F}
};
}


static inline unsigned Font_Word(unsigned char const * ptr)
{
	return((unsigned)ptr[0] | ((unsigned)ptr[1] << 8));
}


/***************************************************************************
 * Buffer_Print -- Text print to graphic buffer routine                    *
 *                                                                         *
 * INPUT:   this_object -- GraphicViewPortClass to print to                *
 *          string      -- text; 10 (LF) returns to x = 0, 13 (CR) to the  *
 *                         starting x, one font height (+FontYSpacing) down*
 *          x_pixel, y_pixel -- position of the first character            *
 *          fcolor, bcolor   -- foreground / background (0 = transparent)  *
 *                                                                         *
 * OUTPUT:  Address where the next character would be drawn, or 0 if the   *
 *          text ran off the bottom of the viewport (or no font is set).   *
 *                                                                         *
 * WARNINGS: Characters that would pass the right edge wrap to the next    *
 *           line at the starting x. Nothing is clipped.                   *
 *                                                                         *
 * HISTORY:                                                                *
 *   01/17/1995 PWG : Created.                                             *
 *=========================================================================*/
extern "C" LONG __cdecl Buffer_Print(void *this_object, const char *string, int x_pixel, int y_pixel, int fcolor, int bcolor)
{
#ifdef WEBCANDC_HEADLESS
	/*
	**	Test harness: log each distinct string as it is drawn.
	*/
	{
		static char last[256];
		if (string && strncmp(last, string, sizeof(last) - 1)) {
			strncpy(last, string, sizeof(last) - 1);
			fprintf(stderr, "[text] %s\n", string);
		}
	}
#endif
	GraphicViewPortClass * vp = (GraphicViewPortClass *)this_object;

	/*
	**	Where to draw.
	*/
	int vpheight = vp->Get_Height();
	int vpwidth = vp->Get_Width();
	int bufferwidth = vpwidth + vp->Get_XAdd() + vp->Get_Pitch();
	unsigned char * curline = (unsigned char *)vp->Get_Offset() + bufferwidth * y_pixel;
	unsigned char * startdraw = curline + x_pixel;
	int original_x = x_pixel;

	unsigned char const * font = (unsigned char const *)FontPtr;
	if (font == NULL) {
		return(0);
	}

	unsigned char const * offsetblock = font + Font_Word(font + FONTOFFSETBLOCK);
	unsigned char const * infoblock = font + Font_Word(font + FONTINFOBLOCK);
	unsigned char const * widthblock = font + Font_Word(font + FONTWIDTHBLOCK);
	unsigned char const * heightblock = font + Font_Word(font + FONTHEIGHTBLOCK);

	/*
	**	The tallest character must fit on this line.
	*/
	int maxheight = infoblock[FONTINFOMAXHEIGHT];
	if (maxheight + y_pixel > vpheight) {
		return(0);
	}
	y_pixel += maxheight;

	/*
	**	Set the foreground and background colours.
	*/
	ColorXlat[0][1] = (unsigned char)fcolor;
	ColorXlat[1][0] = (unsigned char)fcolor;
	ColorXlat[0][0] = (unsigned char)bcolor;
	unsigned char const * xlat = &ColorXlat[0][0];

	for (;;) {
		unsigned char chr = (unsigned char)*string;
		if (chr == 0) {
			break;
		}
		string++;

		int feed_char = chr;
		if (chr != 10 && chr != 13) {
			unsigned char * dest = startdraw;
			int width = widthblock[chr];
			int next_x = x_pixel + width + FontXSpacing;
			startdraw += width + FontXSpacing;

			if (next_x <= vpwidth) {
				x_pixel = next_x;
				int nextdraw = bufferwidth - width;

				unsigned char const * data = font + Font_Word(offsetblock + chr * 2);
				int topblank = heightblock[chr * 2];
				int charheight = heightblock[chr * 2 + 1];
				int bottomblank = (unsigned char)(maxheight - (unsigned char)(topblank + charheight));

				/*
				**	Blank rows above the character: fill with the background colour, or
				**	just skip them when it is transparent.
				*/
				if (topblank) {
					unsigned char color = xlat[0];
					if (color == 0) {
						dest += topblank * bufferwidth;
					} else {
						for (int row = topblank; row > 0; row--) {
							memset(dest, color, width);
							dest += bufferwidth;
						}
					}
				}

				/*
				**	No data rows means nothing else is drawn (not even the bottom blank).
				*/
				if (charheight == 0) {
					continue;
				}

				/*
				**	Character data: two pixels per byte, low nibble first; each row starts on
				**	a new byte. Colour 0 after translation is transparent.
				*/
				for (int row = charheight; row > 0; row--) {
					int remaining = width;
					while (remaining > 0) {
						unsigned char packed = *data++;
						unsigned char color = xlat[packed & 0x0F];
						if (color) *dest = color;
						dest++;
						if (--remaining == 0) break;

						color = xlat[packed & 0xF0];
						if (color) *dest = color;
						dest++;
						remaining--;
					}
					dest += nextdraw;
				}

				/*
				**	Blank rows below the character.
				*/
				if (bottomblank) {
					unsigned char color = xlat[0];
					if (color) {
						for (int row = bottomblank; row > 0; row--) {
							memset(dest, color, width);
							dest += bufferwidth;
						}
					}
				}
				continue;
			}

			/*
			**	The character doesn't fit: back up and force a line feed to the left margin.
			*/
			string--;
			feed_char = 0;
		}

		/*
		**	Line feed. <LF> returns to the left edge, <CR> (and a forced wrap) to the
		**	starting x position.
		*/
		int advance = maxheight + FontYSpacing;
		if (y_pixel + advance > vpheight) {
			return(0);
		}
		curline += bufferwidth * advance;
		y_pixel += advance;
		x_pixel = (feed_char == 10) ? 0 : original_x;
		startdraw = curline + x_pixel;
	}

	return((LONG)startdraw);
}


/***************************************************************************
 * GET_FONT_PALETTE_PTR -- Returns a pointer to the 256 byte font palette  *
 *                                                                         *
 * HISTORY:                                                                *
 *   08/18/1995 PWG : Created.                                             *
 *=========================================================================*/
extern "C" void * __cdecl Get_Font_Palette_Ptr(void)
{
	return(&ColorXlat[0][0]);
}
