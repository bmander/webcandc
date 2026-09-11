/*
** webcandc: portable C++ port of SETSHAPE.ASM (Westwood 32-bit library).
**
**   Set_Shape_Buffer -- registers the shape staging buffer
**
** Also defines the staging-buffer globals it sets (_ShapeBuffer and
** _ShapeBufferSize, declared extern "C" in SHAPE.H). The game uses them
** directly as general scratch space (INI.CPP, EXPAND.CPP, MPLAYER.CPP, ...)
** and ASM_Set_Mouse_Cursor (WWMOUSE.CPP) uses the buffer to uncompress cursor
** shapes.
*/

#include "SHAPE.H"

extern "C" {
long _ShapeBufferSize = 0;
char *_ShapeBuffer = NULL;
}


/***************************************************************************
 * Set_Shape_Buffer -- Sets the shape staging buffer.                      *
 *                                                                         *
 * INPUT:   buffer - pointer to the buffer (may be NULL to release it)     *
 *          size   - size of the buffer in bytes                           *
 *=========================================================================*/
extern "C" void __cdecl Set_Shape_Buffer(void const *buffer, int size)
{
	_ShapeBufferSize = size;
	_ShapeBuffer = (char *)buffer;
}
