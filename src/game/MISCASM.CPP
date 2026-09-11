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
**	webcandc: MISCASM.CPP -- portable C++ for the game's small assembly routines.
**
**	Replaces:
**		- the Watcom "#pragma aux" bodies in FUNCTION.H (Coord_Cell, Distance_Coord),
**		  JSHELL.H (Set_Bit, Get_Bit, First_True_Bit, First_False_Bit, Bound,
**		  Fixed_To_Cardinal, Cardinal_To_Fixed) and COORD.CPP (calcx, calcy);
**		- SUPPORT.ASM (strtrim, Fat_Put_Pixel, Conquer_Build_Fading_Table,
**		  Remove_From_List, Get_EAX) and COORDA.ASM;
**		- the engine routines the game calls from WWLIB asm: FACING8/FACING16/FACINGFF.ASM
**		  (Desired_Facing8/16/256), REVERSE.ASM, CRC.ASM, MEM_COPY.ASM, SHAKESCR.ASM.
**
**	References: reference/CnC_Remastered_Collection/TIBERIANDAWN/MiscAsm.cpp (MSVC __asm
**	version of the same code), reference/Vanilla-Conquer/common/{misc,face,fading}.cpp and
**	tiberiandawn/coord.cpp (portable versions), RA WIN32LIB/MISC/CRC.ASM and SHAKESCR.ASM.
**
**	The original assembly is the specification: register widths, signed/unsigned compares
**	and table contents are reproduced exactly. Places where the port deliberately differs
**	from the asm (only where the asm would run away or read outside its buffer) are marked
**	"webcandc:".
*/

#include	"function.h"


/***********************************************************************************************
 * Mem_Copy -- Copies a block of memory (MEM_COPY.ASM).                                        *
 *                                                                                             *
 *    webcandc: memmove, so overlapping blocks are also handled.                               *
 *=============================================================================================*/
extern "C" void __cdecl Mem_Copy(void const *source, void *dest, unsigned long bytes_to_copy)
{
	memmove(dest, source, bytes_to_copy);
}


/***********************************************************************************************
 * Coord_Cell -- Converts a coordinate into a cell number (FUNCTION.H pragma).                 *
 *                                                                                             *
 *    The cell is (Y cell << 6) | X cell, where the cell parts are the high bytes of the       *
 *    16 bit X and Y lepton components.                                                        *
 *=============================================================================================*/
CELL Coord_Cell(COORDINATE coord)
{
	unsigned int value = (unsigned int)coord;

	return (CELL)((((value >> 16) & 0xFF00U) >> 2) | ((value >> 8) & 0xFFU));
}


/***********************************************************************************************
 * Distance_Coord -- Lepton distance between two coordinates (FUNCTION.H pragma).              *
 *                                                                                             *
 *    "Dragon Strike" distance: larger axis delta plus half the smaller one. All arithmetic    *
 *    is on 16 bit registers with signed compares, as in the original.                         *
 *=============================================================================================*/
int Distance_Coord(COORDINATE coord1, COORDINATE coord2)
{
	short x1 = (short)(coord1 & 0xFFFF);
	short x2 = (short)(coord2 & 0xFFFF);
	short y1 = (short)((coord1 >> 16) & 0xFFFF);
	short y2 = (short)((coord2 >> 16) & 0xFFFF);

	unsigned short dx = (x1 > x2) ? (unsigned short)(x1 - x2) : (unsigned short)(x2 - x1);
	unsigned short dy = (y1 > y2) ? (unsigned short)(y1 - y2) : (unsigned short)(y2 - y1);

	/*
	**	"cmp ax,dx / jg ok / xchg ax,dx" -- AX ends up with the (signed) larger value.
	*/
	if (!((short)dy > (short)dx)) {
		unsigned short temp = dy;
		dy = dx;
		dx = temp;
	}
	return (unsigned short)(dy + (unsigned short)(dx >> 1));
}


/***********************************************************************************************
 * Set_Bit / Get_Bit / First_True_Bit / First_False_Bit (JSHELL.H pragmas).                   *
 *                                                                                             *
 *    The asm works on little-endian dwords with bt/bts/btr/bsf; on a little-endian target     *
 *    that is exactly the byte-wise bit numbering used here.                                   *
 *=============================================================================================*/
void Set_Bit(void * array, int bit, int value)
{
	unsigned char * ptr = (unsigned char *)array + ((unsigned)bit >> 3);
	unsigned char mask = (unsigned char)(1 << (bit & 7));

	if (value) {
		*ptr |= mask;
	} else {
		*ptr &= (unsigned char)~mask;
	}
}


int Get_Bit(void const * array, int bit)
{
	unsigned char const * ptr = (unsigned char const *)array + ((unsigned)bit >> 3);

	return((*ptr >> (bit & 7)) & 1);
}


int First_True_Bit(void const * array)
{
	unsigned char const * ptr = (unsigned char const *)array;
	int index = 0;

	while (*ptr == 0) {
		ptr++;
		index += 8;
	}
	return(index + __builtin_ctz(*ptr));
}


int First_False_Bit(void const * array)
{
	unsigned char const * ptr = (unsigned char const *)array;
	int index = 0;

	while (*ptr == 0xFF) {
		ptr++;
		index += 8;
	}
	return(index + __builtin_ctz((unsigned char)~*ptr));
}


/***********************************************************************************************
 * Bound -- Clips a value to a range (JSHELL.H pragma).                                       *
 *                                                                                             *
 *    The limits are swapped first if they are given in the wrong order.                       *
 *=============================================================================================*/
int Bound(int original, int min, int max)
{
	if (!(min < max)) {
		int temp = min;
		min = max;
		max = temp;
	}
	if (!(original > min)) original = min;
	if (!(original < max)) original = max;
	return(original);
}


/***********************************************************************************************
 * Fixed_To_Cardinal -- Converts a fixed point number into a cardinal number.                  *
 *                                                                                             *
 *    (base * fixed + 128) / 256, or 0xFFFF if the 32 bit product+round overflows 24 bits.     *
 *    JSHELL.H pragma and COORDA.ASM agree on this one.                                        *
 *=============================================================================================*/
unsigned Fixed_To_Cardinal(unsigned base, unsigned fixed)
{
	unsigned value = (unsigned)(base * fixed) + 0x80U;

	if (value & 0xFF000000U) {
		return(0xFFFFU);
	}
	return(value >> 8);
}


/***********************************************************************************************
 * Cardinal_To_Fixed -- Converts cardinal numbers into a fixed point number.                   *
 *                                                                                             *
 *    (cardinal << 8) / base. Game code used the JSHELL.H pragma body, which returns the       *
 *    cardinal value unchanged when base is zero ("or ebx,ebx / jz fini"). COORDA.ASM (and the *
 *    Remastered/Vanilla ports) return 0xFFFF in that case instead; the pragma wins here.      *
 *=============================================================================================*/
unsigned Cardinal_To_Fixed(unsigned base, unsigned cardinal)
{
	if (base == 0) {
		return(cardinal);
	}
	return((unsigned)(cardinal << 8) / base);
}


/***********************************************************************************************
 * calcx / calcy -- Scaled sine/cosine helpers for Move_Point (COORD.CPP pragmas).             *
 *                                                                                             *
 *    16x16 signed multiply, result is bits 7..22 of the product (i.e. product >> 7 in AX).    *
 *    calcy negates the result (inverted sine table). Callers only use the low 16 bits.        *
 *=============================================================================================*/
int calcx(signed short param1, short distance)
{
	return (short)(((int)param1 * (int)distance) >> 7);
}


int calcy(signed short param1, short distance)
{
	return -(int)(short)(((int)param1 * (int)distance) >> 7);
}


/***********************************************************************************************
 * Desired_Facing8 -- Determines facing to reach a position (FACING8.ASM).                    *
 *                                                                                             *
 *    Returns 0..255 in steps of 32. The index is built from: bit 3 = y1-y2 negative,         *
 *    bit 2 = x2-x1 negative, bit 1 = |dx| < |dy|, bit 0 = close to the major axis.            *
 *    The sign tests are on the (wrapping) difference, like "jns".                             *
 *=============================================================================================*/
extern "C" int __cdecl Desired_Facing8(int x1, int y1, int x2, int y2)
{
	static unsigned char const _new_facing8[16] = {1,2,1,0,7,6,7,0,3,2,3,4,5,6,5,4};

	unsigned index = 0;
	unsigned ydiff = (unsigned)y1 - (unsigned)y2;
	if (ydiff & 0x80000000U) {
		index++;
		ydiff = 0U - ydiff;
	}

	index <<= 1;
	unsigned xdiff = (unsigned)x2 - (unsigned)x1;
	if (xdiff & 0x80000000U) {
		index++;
		xdiff = 0U - xdiff;
	}

	/*
	**	"cmp ecx,edx / jb / xchg / rcl" -- carry is (|dx| < |dy|); EDX ends up the larger.
	*/
	unsigned carry = (xdiff < ydiff) ? 1U : 0U;
	unsigned lesser = carry ? xdiff : ydiff;
	unsigned greater = carry ? ydiff : xdiff;
	index = (index << 1) | carry;

	unsigned half = (greater + 1U) >> 1;
	index = (index << 1) | ((lesser < half) ? 1U : 0U);

	return((int)_new_facing8[index] << 5);
}


/***********************************************************************************************
 * Desired_Facing16 -- Converts coordinates into a facing number (FACING16.ASM).              *
 *                                                                                             *
 *    Returns 0..255 in steps of 16. Same index scheme as Desired_Facing8 plus a "very far     *
 *    from the major axis" bit. Table entries of -1 (unreachable) come back as 0xFF0.          *
 *=============================================================================================*/
extern "C" int __cdecl Desired_Facing16(int x1, int y1, int x2, int y2)
{
	static signed char const _new_facing16[32] = {
		 3, 2, 4,-1, 1, 2,0,-1,
		13,14,12,-1,15,14,0,-1,
		 5, 6, 4,-1, 7, 6,8,-1,
		11,10,12,-1, 9,10,8,-1
	};

	unsigned index = 0;
	unsigned ydiff = (unsigned)y1 - (unsigned)y2;
	if (ydiff & 0x80000000U) {
		index++;
		ydiff = 0U - ydiff;
	}

	index <<= 1;
	unsigned xdiff = (unsigned)x2 - (unsigned)x1;
	if (xdiff & 0x80000000U) {
		index++;
		xdiff = 0U - xdiff;
	}

	unsigned carry = (xdiff < ydiff) ? 1U : 0U;
	unsigned lesser = carry ? xdiff : ydiff;
	unsigned greater = carry ? ydiff : xdiff;
	index = (index << 1) | carry;

	unsigned quarter = (((greater + 1U) >> 1) + 1U) >> 1;
	index = (index << 1) | ((lesser < quarter) ? 1U : 0U);

	greater -= quarter;
	index = (index << 1) | ((greater < lesser) ? 1U : 0U);

	return((int)(unsigned char)_new_facing16[index] << 4);
}


/***********************************************************************************************
 * Desired_Facing256 -- Desired facing algorithm 0..255 resolution (FACINGFF.ASM).            *
 *                                                                                             *
 *    Returns 0..255 (North = 0, East = 64). Identical coordinates give 255.                   *
 *=============================================================================================*/
extern "C" int __cdecl Desired_Facing256(int srcx, int srcy, int dstx, int dsty)
{
	unsigned facing = 0;

	/*
	**	Absolute X delta and left/right direction ("jge" = signed compare of the operands).
	*/
	unsigned xdiff = (unsigned)dstx - (unsigned)srcx;
	if (dstx < srcx) {
		xdiff = 0U - xdiff;
		facing = 0xC0;
	}

	/*
	**	Absolute Y delta and top/bottom direction.
	*/
	unsigned ydiff = (unsigned)srcy - (unsigned)dsty;
	if (srcy < dsty) {
		facing ^= 0x40;
		ydiff = 0U - ydiff;
	}

	/*
	**	Quadrant adjust value, then make "big" the larger delta (unsigned "jb").
	*/
	unsigned adjust = (facing & 0x40) ^ 0x40;
	unsigned small = ydiff;
	unsigned big = xdiff;
	if (!(small < big)) {
		small = xdiff;
		big = ydiff;
		adjust ^= 0x40;
	}

	/*
	**	Reduce precision so the divide can't overflow, unless the smaller delta already has
	**	a non-zero high part.
	*/
	if ((small & 0xFFFFFF00U) == 0) {
		while (big & 0xFFFFFF00U) {
			big >>= 1;
			small >>= 1;
		}
	}

	unsigned angle;
	if (big == 0) {
		angle = 0xFFFFFFFFU;
	} else {
		angle = (unsigned)(((unsigned long long)small << 8) / big);
	}

	angle >>= 3;
	if (adjust) {
		adjust--;
		angle = 0U - angle;
	}
	return((int)((angle + adjust + facing) & 0xFFU));
}


/***********************************************************************************************
 * Reverse_Long / Reverse_Short / Swap_Long -- Byte and word order swaps (REVERSE.ASM).        *
 *=============================================================================================*/
extern "C" long __cdecl Reverse_Long(long number)
{
	unsigned int value = (unsigned int)number;

	value = (value >> 24) | ((value >> 8) & 0xFF00U) | ((value << 8) & 0xFF0000U) | (value << 24);
	return((long)(int)value);
}


extern "C" short __cdecl Reverse_Short(short number)
{
	unsigned short value = (unsigned short)number;

	return((short)(unsigned short)((value >> 8) | (value << 8)));
}


extern "C" long __cdecl Swap_Long(long number)
{
	unsigned int value = (unsigned int)number;

	return((long)(int)((value >> 16) | (value << 16)));
}


/***********************************************************************************************
 * Calculate_CRC -- Westwood rolling CRC of a data block (CRC.ASM).                            *
 *                                                                                             *
 *    crc = rol(crc,1) + dword, over the block as little-endian dwords; a trailing 1..3 bytes  *
 *    are packed little-endian into a final dword. This is the MIX file name ID function, so   *
 *    it must match the original bit for bit.                                                  *
 *                                                                                             *
 *    webcandc: CRC.ASM only masks DL when computing the remainder count, so for lengths of    *
 *    256+ that are not a multiple of 4 the original byte loop reads (len & 0xFF00) | (len & 3)*
 *    bytes -- far past the buffer. Here the remainder is always len & 3 (identical for        *
 *    lengths below 256, which covers file names and chat messages).                           *
 *=============================================================================================*/
extern "C" long __cdecl Calculate_CRC(void *buffer, long length)
{
	unsigned char const * ptr = (unsigned char const *)buffer;
	unsigned int crc = 0;
	unsigned int count = (unsigned int)length;

	if (count == 0) {
		return(0);
	}

	for (unsigned int longs = count >> 2; longs; longs--) {
		unsigned int value = (unsigned int)ptr[0] | ((unsigned int)ptr[1] << 8) |
								((unsigned int)ptr[2] << 16) | ((unsigned int)ptr[3] << 24);
		ptr += 4;
		crc = ((crc << 1) | (crc >> 31)) + value;
	}

	unsigned int remainder = count & 3;
	if (remainder) {
		unsigned int value = 0;
		for (unsigned int index = 0; index < remainder; index++) {
			value |= (unsigned int)ptr[index] << (index * 8);
		}
		crc = ((crc << 1) | (crc >> 31)) + value;
	}
	return((long)(int)crc);
}


/***********************************************************************************************
 * Shake_Screen -- Shakes the visible screen the number of times indicated (SHAKESCR.ASM).    *
 *                                                                                             *
 *    The DOS routine moved the CRTC start address 4 scan lines (of 320x200) down for two      *
 *    vertical retraces, then back, per shake -- i.e. the picture jumps up and back. This does *
 *    the same to SeenBuff: 8 lines at 640x400, about 2 ticks shifted and 1 tick normal per    *
 *    shake. The mouse is hidden so the cursor save-under stays consistent.                    *
 *                                                                                             *
 *    NOTE: the Win95 WIN32LIB SHAKESCR.ASM starts with "ret", so C&C95 itself never shook.    *
 *    To match that exactly, make this function return immediately.                           *
 *=============================================================================================*/
static void Shake_Wait(long ticks)
{
	CountDownTimerClass timer;

	timer.Set(ticks);
	while (timer.Time()) {
	}
}


static bool Shake_Copy_To_Page(GraphicViewPortClass & page, unsigned char const * image, int width, int height, int pitch, int shift)
{
	if (!page.Lock()) {
		return(false);
	}

	unsigned char * row = (unsigned char *)page.Get_Offset();
	for (int y = 0; y < height; y++) {
		if (y + shift < height) {
			memcpy(row, image + (y + shift) * width, width);
		} else {
			memset(row, 0, width);
		}
		row += pitch;
	}
	page.Unlock();
	return(true);
}


extern "C" void __cdecl Shake_Screen(int shakes)
{
	GraphicViewPortClass & page = SeenBuff;
	int width = page.Get_Width();
	int height = page.Get_Height();
	int pitch = width + page.Get_XAdd() + page.Get_Pitch();
	int shift = height / 50;			// 4 lines of 200, 8 of 400.

	if (shakes <= 0 || width <= 0 || shift <= 0) {
		return;
	}

	unsigned char * image = new unsigned char [width * height];
	if (image == NULL) {
		return;
	}

	Hide_Mouse();
	if (page.Lock()) {
		unsigned char const * row = (unsigned char const *)page.Get_Offset();
		for (int y = 0; y < height; y++) {
			memcpy(image + y * width, row, width);
			row += pitch;
		}
		page.Unlock();

		while (shakes--) {
			bool shifted = Shake_Copy_To_Page(page, image, width, height, pitch, shift);
			Shake_Wait(2);
			Shake_Copy_To_Page(page, image, width, height, pitch, 0);
			if (!shifted) break;
			Shake_Wait(1);
		}
	}
	Show_Mouse();

	delete [] image;
}


/***********************************************************************************************
 * strtrim -- Remove the leading and trailing white space from a string (SUPPORT.ASM).         *
 *                                                                                             *
 *    Only space and TAB count as white space.                                                 *
 *    webcandc: for an all-blank string the asm stored the terminator and then kept copying    *
 *    the bytes that follow it in memory; the resulting string ("") is the same here, without  *
 *    touching memory past the terminator.                                                     *
 *=============================================================================================*/
extern "C" void __cdecl strtrim(char *buffer)
{
	if (buffer == NULL) return;

	char * source = buffer;
	while (*source == ' ' || *source == '\t') {
		source++;
	}

	if (*source == '\0') {
		*buffer = '\0';
		return;
	}

	if (source != buffer) {
		memmove(buffer, source, strlen(source) + 1);
	}

	/*
	**	buffer[0] is not white space here, so this can't run off the front.
	*/
	char * end = buffer + strlen(buffer) - 1;
	while (*end == ' ' || *end == '\t') {
		*end-- = '\0';
	}
}


/***********************************************************************************************
 * Fat_Put_Pixel -- Draws a fat (size x size) pixel (SUPPORT.ASM).                            *
 *                                                                                             *
 *    Only the upper left corner is range checked (unsigned compares, so negative values are   *
 *    rejected); the block itself is not clipped, exactly as in the original.                 *
 *=============================================================================================*/
extern "C" void __cdecl Fat_Put_Pixel(int x, int y, int color, int size, GraphicViewPortClass &gpage)
{
	/*
	**	webcandc: the asm only tests for zero; a negative size would make "rep stosb" run away.
	*/
	if (size <= 0) return;

	if ((unsigned)y >= (unsigned)gpage.Get_Height()) return;
	if ((unsigned)gpage.Get_Width() <= (unsigned)x) return;

	int pitch = gpage.Get_Width() + gpage.Get_XAdd() + gpage.Get_Pitch();
	unsigned char * ptr = (unsigned char *)gpage.Get_Offset() + y * pitch + x;

	for (int row = 0; row < size; row++) {
		memset(ptr, (unsigned char)color, size);
		ptr += pitch;
	}
}


/***********************************************************************************************
 * Conquer_Build_Fading_Table -- Builds custom shadow/light fading table (SUPPORT.ASM).       *
 *                                                                                             *
 *    Colors 1..239 are remapped to the closest of the last 16 palette entries to the color    *
 *    faded "frac"/256 of the way toward "color". Colors 240..255 map to themselves and color  *
 *    0 stays 0, so a shadow can be drawn repeatedly without getting darker.                   *
 *    Returns dest (also when palette or dest is NULL, as the asm did).                        *
 *=============================================================================================*/
extern "C" void * __cdecl Conquer_Build_Fading_Table(void const *palette, void *dest, int color, int frac)
{
	enum {
		ALLOWED_COUNT = 16,
		ALLOWED_START = 256 - ALLOWED_COUNT
	};

	if (palette == NULL || dest == NULL) {
		return(dest);
	}

	unsigned char const * pal = (unsigned char const *)palette;
	unsigned char * remap = (unsigned char *)dest;

	/*
	**	Fractions above 255 become 255 (unsigned compare).
	*/
	if ((unsigned)frac >= 0x100U) {
		frac = 0xFF;
	}
	signed char fraction = (signed char)(frac >> 1);

	unsigned char const * target = pal + color * 3;

	/*
	**	Transparent black never gets remapped.
	*/
	remap[0] = 0;

	for (int index = 1; index < ALLOWED_START; index++) {
		unsigned char const * orig = pal + index * 3;
		unsigned char ideal[3];

		/*
		**	new = orig - ((orig-target) * fraction), computed as
		**	"sub al,target / imul dl / shl eax,1 / sub dh,ah".
		*/
		for (int gun = 0; gun < 3; gun++) {
			signed char diff = (signed char)(unsigned char)(orig[gun] - target[gun]);
			unsigned short product = (unsigned short)(short)(diff * fraction);
			unsigned char high = (unsigned char)((unsigned short)(product << 1) >> 8);
			ideal[gun] = (unsigned char)(orig[gun] - high);
		}

		/*
		**	Closest of the allowed colors by sum of squared (8 bit) differences. The first
		**	smallest wins; a perfect match stops the search.
		*/
		unsigned char matchcolor = (unsigned char)color;
		unsigned int matchvalue = 0xFFFFFFFFU;
		unsigned char const * entry = pal + ALLOWED_START * 3;

		for (int allowed = 0; allowed < ALLOWED_COUNT; allowed++, entry += 3) {
			unsigned int value = 0;
			for (int gun = 0; gun < 3; gun++) {
				signed char diff = (signed char)(unsigned char)(entry[gun] - ideal[gun]);
				value += (unsigned int)(diff * diff);
			}

			if (value == 0) {
				matchcolor = (unsigned char)(ALLOWED_START + allowed);
				break;
			}
			if (value < matchvalue) {
				matchvalue = value;
				matchcolor = (unsigned char)(ALLOWED_START + allowed);
			}
		}
		remap[index] = matchcolor;
	}

	/*
	**	The remainder of the remap table maps colors to themselves.
	*/
	for (int index = ALLOWED_START; index < 256; index++) {
		remap[index] = (unsigned char)index;
	}
	return(dest);
}


/***********************************************************************************************
 * Remove_From_List -- Removes a pointer from a list of pointers (SUPPORT.ASM).               *
 *                                                                                             *
 *    The trailing pointers are moved down to fill the hole and the count is decremented.     *
 *    The asm tested the counts with "jcxz" (16 bit CX); that is kept.                         *
 *=============================================================================================*/
extern "C" void __cdecl Remove_From_List(void **list, int *index, void * ptr)
{
	int count = *index;

	/*
	**	webcandc: a negative count would have made "repne scasd" scan all of memory.
	*/
	if (count <= 0 || (count & 0xFFFF) == 0 || list == NULL) {
		return;
	}

	for (int pos = 0; pos < count; pos++) {
		if (list[pos] == ptr) {
			int remaining = count - pos - 1;
			if (remaining & 0xFFFF) {
				memmove(&list[pos], &list[pos + 1], remaining * sizeof(list[0]));
			}
			(*index)--;
			return;
		}
	}
}


/***********************************************************************************************
 * Get_EAX -- Returned whatever was in EAX (SUPPORT.ASM).                                      *
 *                                                                                             *
 *    INIT.CPP used it to grab Watcom's srand() seed pointer into RandSeedPtr, which nothing   *
 *    dereferences. There is no meaningful value to return.                                   *
 *=============================================================================================*/
extern "C" long __cdecl Get_EAX(void)
{
	return(0);
}
