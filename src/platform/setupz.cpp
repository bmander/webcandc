/*
** webcandc: unpack the C&C95 installer payload (INSTALL/SETUP.Z on the
** freeware disc) inside the browser, so a player can hand the page the disc
** image instead of an installed copy. Same format as tools/isz_extract.c:
** an InstallShield 3 archive whose files are PKWARE DCL "implode" streams,
** decompressed with blast() (zlib contrib, blast.c).
**
** Called from the loader page (web/shell.html) through ccall:
**     webcandc_extract_setup_z("/tmp/SETUP.Z", "/data", ".MIX")
** Returns the number of files written, or -1 if the archive is unusable.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
WEBCANDC_SYSTEM_HEADERS_BEGIN
#include <emscripten.h>
WEBCANDC_SYSTEM_HEADERS_END
extern "C" {
#include "blast.h"		// zlib contrib; its header has no extern "C" guard
}

static unsigned char *Data;
static long DataSize;

static unsigned rd16(long o) { return Data[o] | (Data[o + 1] << 8); }
static unsigned long rd32(long o) { return (unsigned long)Data[o] | ((unsigned long)Data[o + 1] << 8) | ((unsigned long)Data[o + 2] << 16) | ((unsigned long)Data[o + 3] << 24); }

struct InBuf { unsigned char *p; unsigned left; };

static unsigned in_fn(void *how, unsigned char **buf)
{
	InBuf *in = (InBuf *)how;
	unsigned n = in->left;
	*buf = in->p;
	in->p += n;
	in->left = 0;
	return n;
}

static int out_fn(void *how, unsigned char *buf, unsigned len)
{
	return fwrite(buf, 1, len, (FILE *)how) != len;
}

static bool Has_Suffix(char const *name, char const *filter)
{
	if (!filter || !*filter) return true;
	size_t n = strlen(name), f = strlen(filter);
	return n >= f && !strcasecmp(name + n - f, filter);
}

extern "C" EMSCRIPTEN_KEEPALIVE int webcandc_extract_setup_z(char const *archive, char const *outdir, char const *filter)
{
	FILE *f = fopen(archive, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	DataSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	Data = (unsigned char *)malloc(DataSize);
	bool ok = Data && fread(Data, 1, DataSize, f) == (size_t)DataSize;
	fclose(f);
	if (!ok || DataSize < 64 || rd32(0) != 0x8C655D13UL) {
		free(Data);
		return -1;
	}

	long pos = 4 + 8 + 2 + 4 + 4 + 19;
	long toc = (long)rd32(pos);
	pos += 4 + 4;
	unsigned dir_count = rd16(pos);
	unsigned dir_files[64];
	pos = toc;
	for (unsigned d = 0; d < dir_count && d < 64; d++) {
		dir_files[d] = rd16(pos);
		pos += rd16(pos + 2);
	}

	long accumulator = 255;		// File data starts here, in directory/file order.
	int written = 0;
	for (unsigned d = 0; d < dir_count && d < 64; d++) {
		for (unsigned i = 0; i < dir_files[d]; i++) {
			unsigned long csize = rd32(pos + 7);
			unsigned chunk = rd16(pos + 23);
			unsigned name_len = Data[pos + 29];
			char name[256];
			memcpy(name, Data + pos + 30, name_len);
			name[name_len] = '\0';
			pos += chunk;
			long start = accumulator;
			accumulator += (long)csize;
			if (!Has_Suffix(name, filter) || start + (long)csize > DataSize) continue;

			char path[512];
			snprintf(path, sizeof(path), "%s/%s", outdir, name);
			for (char *p = path + strlen(outdir) + 1; *p; p++) *p = (char)toupper((unsigned char)*p);
			FILE *out = fopen(path, "wb");
			if (!out) continue;
			InBuf in = { Data + start, (unsigned)csize };
			int rc = blast(in_fn, &in, out_fn, out, NULL, NULL);
			fclose(out);
			if (rc == 0) written++;
			else remove(path);
		}
	}
	free(Data);
	Data = NULL;
	return written;
}
