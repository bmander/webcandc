/*
** webcandc: portable C++ replacements for two viewport assembly routines of the
** Westwood 32-bit library:
**
**   TOBUFF.ASM   -- Buffer_To_Buffer       (GraphicViewPortClass::To_Buffer)
**   SZREGION.ASM -- Buffer_Size_Of_Region  (GraphicViewPortClass::Size_Of_Region)
**
** Both are straight ports of the original assembly, including its clipping
** rules, so behaviour matches the 1995 executable.
*/

#include "GBUFFER.H"

/***************************************************************************
 * Buffer_To_Buffer -- Copies a region of a viewport to a linear buffer.   *
 *                                                                         *
 * INPUT:   thisptr - GraphicViewPortClass to copy from                    *
 *          x, y    - upper left corner of the region on the viewport      *
 *          w, h    - dimensions of the region                             *
 *          buff    - linear destination buffer (row pitch == w)           *
 *          size    - size of the destination buffer in bytes              *
 *                                                                         *
 * OUTPUT:  Width of the copied (clipped) region, or 0 if nothing copied.  *
 *                                                                         *
 * WARNINGS: The region is clipped to the viewport; the parts of the       *
 *           destination corresponding to clipped-off pixels are left      *
 *           untouched. Like the original, the copy is aborted if w*lines  *
 *           exceeds size.                                                 *
 *=========================================================================*/
extern "C" long __cdecl Buffer_To_Buffer(void *thisptr, int x, int y, int w, int h, void *buff, long size)
{
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	if (!vp || !buff) return 0;

	int const vpw = vp->Get_Width();
	int const vph = vp->Get_Height();
	int x1 = x + w;			// exclusive right edge
	int y1 = y + h;			// exclusive bottom edge
	int dest_x1 = 0;
	int dest_y1 = 0;

	/*
	** Cohen-Sutherland style outcodes, as computed by the original:
	** 8 = left of 0, 4 = right of width, 2 = above 0, 1 = below height.
	*/
	unsigned const c = ((x < 0) << 3) | ((x > vpw) << 2) | ((y < 0) << 1) | (y > vph);
	unsigned const d = ((x1 < 0) << 3) | ((x1 > vpw) << 2) | ((y1 < 0) << 1) | (y1 > vph);

	if (c & d) return 0;		// completely off one edge
	if (c | d) {
		if (c & 8) {
			dest_x1 = -x;
			x = 0;
		}
		if (c & 2) {
			dest_y1 = -y;
			y = 0;
		}
		if (d & 4) x1 = vpw;
		if (d & 1) y1 = vph;
	}

	int const lines = y1 - y;
	int const width = x1 - x;
	if (lines <= 0 || width <= 0) return 0;
	if ((long)w * lines > size) return 0;

	long const stride = vp->Get_XAdd() + vpw + vp->Get_Pitch();
	unsigned char const *src = (unsigned char const *)vp->Get_Offset() + y * stride + x;
	unsigned char *dst = (unsigned char *)buff + dest_y1 * w + dest_x1;

	for (int row = 0; row < lines; row++) {
		memcpy(dst, src, width);
		src += stride;
		dst += w;
	}
	return width;
}


/***************************************************************************
 * Buffer_Size_Of_Region -- Calculates the size of a region of a viewport. *
 *                                                                         *
 * INPUT:   thisptr - GraphicViewPortClass the region lies on              *
 *          w, h    - dimensions of the region                             *
 *                                                                         *
 * OUTPUT:  w * h, with each dimension clipped to the viewport size.       *
 *          (Unsigned compares, as in the original: negative dimensions    *
 *          clip to the viewport size.)                                    *
 *=========================================================================*/
extern "C" long __cdecl Buffer_Size_Of_Region(void *thisptr, int w, int h)
{
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	unsigned width = (unsigned)w;
	unsigned height = (unsigned)h;
	unsigned const vpw = (unsigned)vp->Get_Width();
	unsigned const vph = (unsigned)vp->Get_Height();

	if (width >= vpw) width = vpw;
	if (height >= vph) height = vph;
	return (long)(width * height);
}
