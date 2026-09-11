/*
** webcandc: implementations for the DOS/Watcom surface declared in
** src/compat/dosshim.h. There is no real-mode, no interrupts and no ports;
** drive and directory queries map onto the Emscripten virtual file system.
*/
#include "dosshim.h"
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>

extern "C" {

int int386(int, union REGS *in, union REGS *out) { if (out && out != in) *out = *in; if (out) out->x.cflag = 1; return 0; }
int int386x(int no, union REGS *in, union REGS *out, struct SREGS *) { return int386(no, in, out); }
int intdos(union REGS *in, union REGS *out) { return int386(0x21, in, out); }
void segread(struct SREGS *seg) { memset(seg, 0, sizeof(*seg)); }
void _disable(void) {}
void _enable(void) {}
unsigned inp(unsigned) { return 0; }
unsigned inpw(unsigned) { return 0; }
unsigned outp(unsigned, unsigned value) { return value; }
unsigned outpw(unsigned, unsigned value) { return value; }

/*
** DOS file handles are POSIX descriptors. DOS file names are case-blind; the
** Emscripten file system is not, so a failed open retries the upper- and
** lower-cased spelling of the name before giving up.
*/
static int Open_Case_Blind(const char *path, int flags, int mode)
{
	int fd = open(path, flags, mode);
	if (fd >= 0 || (flags & O_CREAT)) return fd;
	char alt[512];
	for (int pass = 0; pass < 2; pass++) {
		snprintf(alt, sizeof(alt), "%s", path);
		char *base = strrchr(alt, '/');
		base = base ? base + 1 : alt;
		for (char *p = base; *p; p++) *p = (char)(pass ? tolower((unsigned char)*p) : toupper((unsigned char)*p));
		fd = open(alt, flags, mode);
		if (fd >= 0) return fd;
	}
	return -1;
}

unsigned _dos_open(const char *path, unsigned mode, int *handle)
{
	/* Only the access bits are open() flags; the SH_* share bits are not. */
	int access = mode & 3;
	int flags = access == 0 ? O_RDONLY : (access == 1 ? O_WRONLY : O_RDWR);
	int fd = Open_Case_Blind(path, flags, 0666);
	if (fd < 0) {
		*handle = -1;
		return errno ? (unsigned)errno : 2;
	}
	*handle = fd;
	return 0;
}

unsigned _dos_creat(const char *path, unsigned, int *handle)
{
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		*handle = -1;
		return errno ? (unsigned)errno : 5;
	}
	*handle = fd;
	return 0;
}

unsigned _dos_close(int handle) { return close(handle) == 0 ? 0 : 6; }

unsigned _dos_read(int handle, void *buf, unsigned count, unsigned *bytes)
{
	ssize_t n = read(handle, buf, count);
	if (n < 0) { *bytes = 0; return errno ? (unsigned)errno : 5; }
	*bytes = (unsigned)n;
	return 0;
}

unsigned _dos_write(int handle, void const *buf, unsigned count, unsigned *bytes)
{
	ssize_t n = write(handle, buf, count);
	if (n < 0) { *bytes = 0; return errno ? (unsigned)errno : 5; }
	*bytes = (unsigned)n;
	return 0;
}

/*
** _dos_findfirst/_dos_findnext: the directory listing is captured at
** findfirst time; the find_t 'reserved' area holds a pointer to it.
*/
struct FindState {
	char Dir[256];
	char Pattern[64];
	DIR *Handle;
};

static bool Next_Match(struct find_t *buf)
{
	FindState *state;
	memcpy(&state, buf->reserved, sizeof(state));
	if (!state || !state->Handle) return false;
	struct dirent *ent;
	while ((ent = readdir(state->Handle)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		if (fnmatch(state->Pattern, ent->d_name, FNM_CASEFOLD) != 0) continue;
		char full[512];
		snprintf(full, sizeof(full), "%s/%s", state->Dir, ent->d_name);
		struct stat st;
		if (stat(full, &st) != 0) continue;
		buf->attrib = S_ISDIR(st.st_mode) ? _A_SUBDIR : _A_NORMAL;
		buf->size = (unsigned long)st.st_size;
		buf->wr_time = 0;
		buf->wr_date = 0;
		size_t i;
		for (i = 0; i < sizeof(buf->name) - 1 && ent->d_name[i]; i++) buf->name[i] = (char)toupper((unsigned char)ent->d_name[i]);
		buf->name[i] = '\0';
		return true;
	}
	return false;
}

unsigned _dos_findfirst(const char *path, unsigned, struct find_t *buf)
{
	FindState *state = (FindState *)calloc(1, sizeof(FindState));
	const char *slash = strrchr(path, '/');
	const char *bslash = strrchr(path, '\\');
	if (bslash > slash) slash = bslash;
	if (slash) {
		size_t n = (size_t)(slash - path);
		if (n >= sizeof(state->Dir)) n = sizeof(state->Dir) - 1;
		memcpy(state->Dir, path, n);
		state->Dir[n] = '\0';
		snprintf(state->Pattern, sizeof(state->Pattern), "%s", slash + 1);
	} else {
		strcpy(state->Dir, ".");
		snprintf(state->Pattern, sizeof(state->Pattern), "%s", path);
	}
	if (strcmp(state->Pattern, "*.*") == 0) strcpy(state->Pattern, "*");
	state->Handle = opendir(state->Dir);
	memcpy(buf->reserved, &state, sizeof(state));
	if (Next_Match(buf)) return 0;
	_dos_findclose(buf);
	return 18;	/* ENOMOREFILES */
}

unsigned _dos_findnext(struct find_t *buf)
{
	if (Next_Match(buf)) return 0;
	return 18;
}

unsigned _dos_findclose(struct find_t *buf)
{
	FindState *state;
	memcpy(&state, buf->reserved, sizeof(state));
	if (state) {
		if (state->Handle) closedir(state->Handle);
		free(state);
		state = NULL;
		memcpy(buf->reserved, &state, sizeof(state));
	}
	return 0;
}

unsigned _dos_getdiskfree(unsigned, struct diskfree_t *df)
{
	df->total_clusters = 200000;
	df->avail_clusters = 100000;
	df->sectors_per_cluster = 8;
	df->bytes_per_sector = 512;
	return 0;
}

void _dos_getdrive(unsigned *drive) { *drive = 3; }	/* C: */
void _dos_setdrive(unsigned, unsigned *total) { if (total) *total = 26; }

unsigned _dos_getfileattr(const char *path, unsigned *attr)
{
	struct stat st;
	if (stat(path, &st) != 0) return 2;
	*attr = S_ISDIR(st.st_mode) ? _A_SUBDIR : _A_NORMAL;
	return 0;
}

int _getdrive(void) { return 3; }
int _chdrive(int) { return 0; }
int getdisk(void) { return 2; }
int setdisk(int) { return 26; }

int sopen(const char *path, int access, int, ...)
{
	int mode = 0666;
	if (access & O_CREAT) {
		va_list ap;
		va_start(ap, access);
		va_arg(ap, int);		/* share already consumed as a named arg */
		va_end(ap);
	}
	return open(path, access, mode);
}

unsigned _bios_keybrd(unsigned) { return 0; }
int spawnl(int, const char *, const char *, ...) { return -1; }
int spawnv(int, const char *, const char *const *) { return -1; }

int kbhit(void) { return 0; }
int getch(void) { return 0; }
void delay(unsigned) {}

}
