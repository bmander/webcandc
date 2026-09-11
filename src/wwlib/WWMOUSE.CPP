/*
** webcandc: portable C++ port of WWMOUSE.ASM (Westwood 32-bit library,
** reference/CnC_Red_Alert/WIN32LIB/KEYBOARD/WWMOUSE.ASM), the software mouse
** cursor primitives used by WWMouseClass (MOUSEWW.CPP):
**
**   Mouse_Shadow_Buffer  -- save/restore the screen area under the cursor
**   Draw_Mouse           -- draw the cursor (color 0 is transparent)
**   ASM_Set_Mouse_Cursor -- decode a shape into the cursor buffer
**
** The assembly addressed WWMouseClass's private fields through a mirrored
** struct (MOUSE.INC). Here they are reached with the standard
** explicit-instantiation access idiom, so the compiler checks every field by
** name and type against MOUSE.H and a layout change there cannot silently
** break this file.
*/

#include "MOUSE.H"
#include "SHAPE.H"
#include "IFF.H"		// LCW_Uncompress

namespace {

/*
** Access to WWMouseClass private members. Explicit template instantiation is
** exempt from access checking, and the friend function it defines hands back
** the pointer-to-member.
*/
template <typename Tag, typename Tag::type Member>
struct WWMouse_Access {
	friend typename Tag::type Mouse_Member(Tag) { return Member; }
};

#define WWMOUSE_MEMBER(name, T)												\
	struct name##_Tag {															\
		typedef T WWMouseClass::*type;										\
		friend type Mouse_Member(name##_Tag);								\
	};																					\
	template struct WWMouse_Access<name##_Tag, &WWMouseClass::name>;

WWMOUSE_MEMBER(MouseCursor, char *)
WWMOUSE_MEMBER(MouseXHot, int)
WWMOUSE_MEMBER(MouseYHot, int)
WWMOUSE_MEMBER(CursorWidth, int)
WWMOUSE_MEMBER(CursorHeight, int)
WWMOUSE_MEMBER(MaxWidth, int)
WWMOUSE_MEMBER(MaxHeight, int)
WWMOUSE_MEMBER(PrevCursor, char *)

#undef WWMOUSE_MEMBER

#define MOUSE_FIELD(obj, name)	((obj)->*Mouse_Member(name##_Tag()))

/*
** Clip the cursor rectangle [x0,x1) x [y0,y1) against the viewport, exactly
** as the assembly does. Returns false if nothing remains visible. skip_x and
** skip_y receive the number of cursor columns/rows clipped off the left/top.
*/
bool Clip_Cursor_Rect(GraphicViewPortClass *vp, int &x0, int &y0, int &x1, int &y1, int &skip_x, int &skip_y)
{
	int const w = vp->Get_Width();
	int const h = vp->Get_Height();

	unsigned const c = ((x0 < 0) << 3) | ((x0 > w) << 2) | ((y0 < 0) << 1) | (y0 > h);
	unsigned const d = ((x1 < 0) << 3) | ((x1 > w) << 2) | ((y1 < 0) << 1) | (y1 > h);

	skip_x = 0;
	skip_y = 0;
	if (c & d) return false;
	if (c | d) {
		if (c & 8) {
			skip_x = -x0;
			x0 = 0;
		}
		if (c & 2) {
			skip_y = -y0;
			y0 = 0;
		}
		if (d & 4) x1 = w;
		if (d & 1) y1 = h;
	}
	return (x1 - x0) > 0 && (y1 - y0) > 0;
}

/*
** Un-RLE a shape's pixel data: a zero byte is followed by a count of
** transparent (zero) pixels. If remap is non-NULL each non-zero pixel is
** looked up in it (16-color "compact" shapes).
*/
void Unrle_Cursor(unsigned char *dst, unsigned char const *src, int pixels, unsigned char const *remap)
{
	while (pixels > 0) {
		unsigned char b = *src++;
		if (b) {
			*dst++ = remap ? remap[b] : b;
			pixels--;
		} else {
			int run = *src++;
			if (run > pixels) run = pixels;		// guard against malformed data
			memset(dst, 0, run);
			dst += run;
			pixels -= run;
		}
	}
}

enum {
	SHAPE_HEADER_SIZE = 10,		// packed Shape_Type without the color table
	SHAPE_COLOR_TABLE_SIZE = 16
};

}	// namespace


/***************************************************************************
 * MOUSE_SHADOW_BUFFER -- Handles storing and restoring the mouse buffer   *
 *                                                                         *
 * INPUT:   thisptr - WWMouseClass (supplies the current cursor size)      *
 *          srcdst  - viewport to save from / restore to                   *
 *          buffer  - shadow buffer (row pitch == cursor width)            *
 *          x, y    - mouse position (hot spot is subtracted here)         *
 *          hotx/y  - hot spot to use                                      *
 *          store   - 1 = save screen into buffer, else restore to screen  *
 *=========================================================================*/
extern "C" void __cdecl Mouse_Shadow_Buffer(void *thisptr, GraphicViewPortClass *srcdst, void *buffer, int x, int y, int hotx, int hoty, int store)
{
	WWMouseClass *mouse = (WWMouseClass *)thisptr;
	if (!mouse || !srcdst || !buffer) return;

	int const cursor_width = MOUSE_FIELD(mouse, CursorWidth);
	int x0 = x - hotx;
	int y0 = y - hoty;
	int x1 = x0 + cursor_width;
	int y1 = y0 + MOUSE_FIELD(mouse, CursorHeight);
	int skip_x, skip_y;

	if (!Clip_Cursor_Rect(srcdst, x0, y0, x1, y1, skip_x, skip_y)) return;

	long const stride = srcdst->Get_Width() + srcdst->Get_XAdd() + srcdst->Get_Pitch();
	unsigned char *screen = (unsigned char *)srcdst->Get_Offset() + y0 * stride + x0;
	unsigned char *shadow = (unsigned char *)buffer + skip_y * cursor_width + skip_x;
	int const width = x1 - x0;
	int rows = y1 - y0;

	if (store == 1) {
		while (rows--) {
			memcpy(shadow, screen, width);
			screen += stride;
			shadow += cursor_width;
		}
	} else {
		while (rows--) {
			memcpy(screen, shadow, width);
			screen += stride;
			shadow += cursor_width;
		}
	}
}


/***************************************************************************
 * DRAW_MOUSE -- Draws the mouse cursor (color 0 is transparent).          *
 *                                                                         *
 * INPUT:   thisptr - WWMouseClass                                         *
 *          srcdst  - viewport to draw on                                  *
 *          x, y    - mouse position (hot spot is subtracted here)         *
 *=========================================================================*/
extern "C" void __cdecl Draw_Mouse(void *thisptr, GraphicViewPortClass *srcdst, int x, int y)
{
	WWMouseClass *mouse = (WWMouseClass *)thisptr;
	if (!mouse || !srcdst) return;

	int const cursor_width = MOUSE_FIELD(mouse, CursorWidth);
	int x0 = x - MOUSE_FIELD(mouse, MouseXHot);
	int y0 = y - MOUSE_FIELD(mouse, MouseYHot);
	int x1 = x0 + cursor_width;
	int y1 = y0 + MOUSE_FIELD(mouse, CursorHeight);
	int skip_x, skip_y;

	if (!Clip_Cursor_Rect(srcdst, x0, y0, x1, y1, skip_x, skip_y)) return;

	long const stride = srcdst->Get_Width() + srcdst->Get_XAdd() + srcdst->Get_Pitch();
	unsigned char *screen = (unsigned char *)srcdst->Get_Offset() + y0 * stride + x0;
	unsigned char const *cursor = (unsigned char const *)MOUSE_FIELD(mouse, MouseCursor) + skip_y * cursor_width + skip_x;
	int const width = x1 - x0;

	for (int rows = y1 - y0; rows > 0; rows--) {
		for (int i = 0; i < width; i++) {
			if (cursor[i]) screen[i] = cursor[i];
		}
		screen += stride;
		cursor += cursor_width;
	}
}


/***************************************************************************
 * ASM_SET_MOUSE_CURSOR -- Sets the shape to be used as the mouse.         *
 *                                                                         *
 * INPUT:   thisptr    - WWMouseClass                                      *
 *          hotspotx/y - offset of the click point into the shape          *
 *          cursor     - shape (as returned by Extract_Shape)              *
 *                                                                         *
 * OUTPUT:  The previous cursor shape pointer.                             *
 *                                                                         *
 * WARNINGS: LCW-compressed shapes are staged through _ShapeBuffer.        *
 *           Shapes larger than the mouse's maximum size are ignored (but  *
 *           still recorded as the current cursor), as in the original.    *
 *=========================================================================*/
extern "C" void * __cdecl ASM_Set_Mouse_Cursor(void * thisptr, int hotspotx, int hotspoty, VOID *cursor)
{
	WWMouseClass *mouse = (WWMouseClass *)thisptr;
	if (!mouse) return NULL;

	if (cursor) {
		int const datasize = Get_Shape_Uncomp_Size(cursor);
		int const width = Get_Shape_Width(cursor);
		int const height = Get_Shape_Original_Height(cursor);

		if (width <= MOUSE_FIELD(mouse, MaxWidth) && height <= MOUSE_FIELD(mouse, MaxHeight)) {
			unsigned char const *shape = (unsigned char const *)cursor;
			unsigned short stype = (unsigned short)(shape[0] | (shape[1] << 8));

			/*
			** Compressed shapes are uncompressed into the shape staging buffer
			** first (header copied, type flagged as uncompressed).
			*/
			if (!(stype & MAKESHAPE_NOCOMP) && _ShapeBuffer) {
				unsigned char *staging = (unsigned char *)_ShapeBuffer;
				int header = SHAPE_HEADER_SIZE;
				if (stype & MAKESHAPE_COMPACT) header += SHAPE_COLOR_TABLE_SIZE;

				stype |= MAKESHAPE_NOCOMP;
				staging[0] = (unsigned char)stype;
				staging[1] = (unsigned char)(stype >> 8);
				memcpy(staging + 2, shape + 2, header - 2);
				LCW_Uncompress((void *)(shape + header), staging + header, (unsigned long)datasize);
				shape = staging;
			}

			unsigned char *dst = (unsigned char *)MOUSE_FIELD(mouse, MouseCursor);
			if (stype & MAKESHAPE_COMPACT) {
				Unrle_Cursor(dst, shape + SHAPE_HEADER_SIZE + SHAPE_COLOR_TABLE_SIZE, width * height, shape + SHAPE_HEADER_SIZE);
			} else {
				Unrle_Cursor(dst, shape + SHAPE_HEADER_SIZE, width * height, NULL);
			}

			MOUSE_FIELD(mouse, MouseXHot) = hotspotx;
			MOUSE_FIELD(mouse, MouseYHot) = hotspoty;
			MOUSE_FIELD(mouse, CursorHeight) = height;
			MOUSE_FIELD(mouse, CursorWidth) = width;
		}
	}

	void *previous = MOUSE_FIELD(mouse, PrevCursor);
	MOUSE_FIELD(mouse, PrevCursor) = (char *)cursor;
	return previous;
}
