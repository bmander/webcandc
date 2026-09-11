/*
** webcandc: implementations of the Watcom / Microsoft C runtime extensions
** declared in watcomlib.h.
*/
#include "watcomlib.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {

int stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
int strcmpi(const char *a, const char *b) { return strcasecmp(a, b); }
int strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }

int memicmp(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = (const unsigned char *)a, *pb = (const unsigned char *)b;
	for (size_t i = 0; i < n; i++) {
		int d = tolower(pa[i]) - tolower(pb[i]);
		if (d) return d;
	}
	return 0;
}

char *strupr(char *s)
{
	for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
	return s;
}

char *strlwr(char *s)
{
	for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
	return s;
}

char *strrev(char *s)
{
	size_t n = strlen(s);
	for (size_t i = 0; i < n / 2; i++) {
		char t = s[i];
		s[i] = s[n - 1 - i];
		s[n - 1 - i] = t;
	}
	return s;
}

static char *utoa_base(unsigned long value, char *buf, int radix, bool negative)
{
	char tmp[40];
	int i = 0;
	if (radix < 2 || radix > 36) radix = 10;
	do {
		int d = (int)(value % (unsigned)radix);
		tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		value /= (unsigned)radix;
	} while (value);
	char *p = buf;
	if (negative) *p++ = '-';
	while (i) *p++ = tmp[--i];
	*p = '\0';
	return buf;
}

char *itoa(int value, char *buf, int radix)
{
	if (radix == 10 && value < 0) return utoa_base((unsigned long)(-(long)value), buf, radix, true);
	return utoa_base((unsigned int)value, buf, radix, false);
}

char *ltoa(long value, char *buf, int radix)
{
	if (radix == 10 && value < 0) return utoa_base((unsigned long)(-value), buf, radix, true);
	return utoa_base((unsigned long)value, buf, radix, false);
}

char *ultoa(unsigned long value, char *buf, int radix) { return utoa_base(value, buf, radix, false); }

void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
	const char *p = path;
	if (drive) drive[0] = '\0';
	if (p[0] && p[1] == ':') {
		if (drive) { drive[0] = p[0]; drive[1] = ':'; drive[2] = '\0'; }
		p += 2;
	}
	const char *slash = NULL;
	for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\') slash = q;
	const char *base = slash ? slash + 1 : p;
	if (dir) {
		size_t n = (size_t)(base - p);
		memcpy(dir, p, n);
		dir[n] = '\0';
	}
	const char *dot = strrchr(base, '.');
	if (!dot) dot = base + strlen(base);
	if (fname) {
		size_t n = (size_t)(dot - base);
		memcpy(fname, base, n);
		fname[n] = '\0';
	}
	if (ext) strcpy(ext, dot);
}

void _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext)
{
	path[0] = '\0';
	if (drive && *drive) strcat(path, drive);
	if (dir && *dir) {
		strcat(path, dir);
		char last = dir[strlen(dir) - 1];
		if (last != '/' && last != '\\') strcat(path, "/");
	}
	if (fname) strcat(path, fname);
	if (ext && *ext) {
		if (*ext != '.') strcat(path, ".");
		strcat(path, ext);
	}
}

long filelength(int fd)
{
	struct stat st;
	if (fstat(fd, &st) != 0) return -1;
	return (long)st.st_size;
}

unsigned int _rotl(unsigned int v, int s) { s &= 31; return s ? (v << s) | (v >> (32 - s)) : v; }
unsigned int _rotr(unsigned int v, int s) { s &= 31; return s ? (v >> s) | (v << (32 - s)) : v; }
unsigned long _lrotl(unsigned long v, int s) { return _rotl((unsigned)v, s); }
unsigned long _lrotr(unsigned long v, int s) { return _rotr((unsigned)v, s); }

}
