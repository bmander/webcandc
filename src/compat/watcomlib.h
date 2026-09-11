/*
** webcandc: Watcom / Microsoft C runtime extensions used by the 1995 code,
** plus the calling-convention and segment keywords that mean nothing on wasm32.
*/
#ifndef WEBCANDC_WATCOMLIB_H
#define WEBCANDC_WATCOMLIB_H

#include <stddef.h>

/* Calling conventions and memory-model keywords. */
#define __cdecl
#define _cdecl
#define cdecl
#define __stdcall
#define _stdcall
#define __pascal
#define _pascal
#define __fastcall
#define __loadds
#define _loadds
#define __saveregs
#define _saveregs
#define __export
#define _export
#define __interrupt
#define _interrupt
#define __far
#define _far
#define far
#define __near
#define _near
#define near
#define __huge
#define _huge
#define huge
#define __based(x)
#define __segment unsigned short

#define _MAX_PATH   260
#define _MAX_DRIVE  3
#define _MAX_DIR    256
#define _MAX_FNAME  256
#define _MAX_EXT    256

#define O_BINARY 0
#define O_TEXT   0
#define _O_BINARY 0

#ifdef __cplusplus
extern "C" {
#endif

int  stricmp(const char *a, const char *b);
int  strcmpi(const char *a, const char *b);
int  strnicmp(const char *a, const char *b, size_t n);
int  memicmp(const void *a, const void *b, size_t n);
char *strupr(char *s);
char *strlwr(char *s);
char *strrev(char *s);
char *itoa(int value, char *buf, int radix);
char *ltoa(long value, char *buf, int radix);
char *ultoa(unsigned long value, char *buf, int radix);
void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext);
void _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext);
long filelength(int fd);
/* _rotl/_rotr/_lrotl/_lrotr are clang builtins under -fms-extensions. */
void randomize(void);
int kbhit(void);
int getch(void);
void delay(unsigned ms);

#define _stricmp stricmp
#define _strnicmp strnicmp
#define _strupr strupr
#define _strlwr strlwr
#define _itoa itoa
#define _ltoa ltoa
#define _ultoa ultoa
#define _memicmp memicmp
#define lrotl _lrotl
#define lrotr _lrotr

#ifdef __cplusplus
}

/* Watcom's <stdlib.h> supplied min/max. Templates, not macros, so the
   C++ standard library headers (already included) are unaffected. */
template<class T> inline T min(T a, T b) { return (b < a) ? b : a; }
template<class T> inline T max(T a, T b) { return (a < b) ? b : a; }
template<class T, class U> inline T min(T a, U b) { return ((T)b < a) ? (T)b : a; }
template<class T, class U> inline T max(T a, U b) { return (a < (T)b) ? (T)b : a; }
#endif

#endif
