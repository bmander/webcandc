/*
** webcandc: the DOS / DPMI / port-I/O surface of Watcom's <dos.h>, <i86.h>,
** <conio.h>, <bios.h>, <direct.h>, <io.h>, <share.h> and <process.h>.
**
** None of this hardware exists in a browser. Register-level interrupt calls
** and port I/O compile to inert stubs; file-system queries map onto POSIX.
*/
#ifndef WEBCANDC_DOSSHIM_H
#define WEBCANDC_DOSSHIM_H

#include <stddef.h>
#include <unistd.h>
#include <sys/stat.h>

/* Register sets for int386()/intdos(). */
struct DWORDREGS { unsigned int eax, ebx, ecx, edx, esi, edi, cflag; };
struct WORDREGS { unsigned short ax, _1, bx, _2, cx, _3, dx, _4, si, _5, di, _6; unsigned int cflag; };
struct BYTEREGS { unsigned char al, ah, _1, _2, bl, bh, _3, _4, cl, ch, _5, _6, dl, dh, _7, _8; };
union REGS { struct DWORDREGS x; struct DWORDREGS w_ext; struct WORDREGS w; struct BYTEREGS h; };
union REGPACK { struct DWORDREGS x; struct WORDREGS w; struct BYTEREGS h; };
struct SREGS { unsigned short es, cs, ss, ds, fs, gs; };

struct find_t {
	char reserved[21];
	char attrib;
	unsigned short wr_time;
	unsigned short wr_date;
	unsigned long size;
	char name[13];
};

struct diskfree_t {
	unsigned total_clusters;
	unsigned avail_clusters;
	unsigned sectors_per_cluster;
	unsigned bytes_per_sector;
};

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_VOLID  0x08
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

#define SH_COMPAT 0x00
#define SH_DENYRW 0x10
#define SH_DENYWR 0x20
#define SH_DENYRD 0x30
#define SH_DENYNO 0x40

#define P_WAIT    0
#define P_NOWAIT  1
#define P_OVERLAY 2

#define MK_FP(seg, ofs) ((void *)(((unsigned long)(seg) << 4) + (unsigned long)(ofs)))
#define FP_SEG(p) ((unsigned short)(((unsigned long)(p)) >> 4))
#define FP_OFF(p) ((unsigned short)(((unsigned long)(p)) & 0xF))

#ifdef __cplusplus
extern "C" {
#endif

int int386(int intno, union REGS *in, union REGS *out);
int int386x(int intno, union REGS *in, union REGS *out, struct SREGS *seg);
int intdos(union REGS *in, union REGS *out);
void segread(struct SREGS *seg);
void _disable(void);
void _enable(void);
unsigned inp(unsigned port);
unsigned inpw(unsigned port);
unsigned outp(unsigned port, unsigned value);
unsigned outpw(unsigned port, unsigned value);

unsigned _dos_open(const char *path, unsigned mode, int *handle);
unsigned _dos_creat(const char *path, unsigned attr, int *handle);
unsigned _dos_close(int handle);
unsigned _dos_read(int handle, void *buf, unsigned count, unsigned *bytes);
unsigned _dos_write(int handle, void const *buf, unsigned count, unsigned *bytes);
unsigned _dos_findfirst(const char *path, unsigned attr, struct find_t *buf);
unsigned _dos_findnext(struct find_t *buf);
unsigned _dos_findclose(struct find_t *buf);
unsigned _dos_getdiskfree(unsigned drive, struct diskfree_t *df);
void _dos_getdrive(unsigned *drive);
void _dos_setdrive(unsigned drive, unsigned *total);
unsigned _dos_getfileattr(const char *path, unsigned *attr);
int _getdrive(void);
int _chdrive(int drive);
int getdisk(void);
int setdisk(int drive);
int sopen(const char *path, int access, int share, ...);
unsigned _bios_keybrd(unsigned cmd);
int spawnl(int mode, const char *path, const char *arg0, ...);
int spawnv(int mode, const char *path, const char *const *argv);

#ifdef __cplusplus
}
#endif

#endif
