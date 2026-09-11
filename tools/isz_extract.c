/*
** isz_extract: unpack an InstallShield 3 ".Z" archive (INSTALL/SETUP.Z on the
** C&C95 discs), the installer payload holding CCLOCAL.MIX, UPDATE.MIX, ...
**
**   cc -O2 -o isz_extract tools/isz_extract.c blast.c   (blast.c: zlib/contrib/blast)
**   isz_extract SETUP.Z OUTDIR [DATA_START]
**
** Layout (as read by OpenRA's InstallShieldPackage): a header with the table of
** contents offset and directory count; directory records; then per-directory
** file records carrying each file's compressed size. File data is stored
** back to back from DATA_START (255) in directory/file order, each file
** compressed with PKWARE DCL "implode", which blast() decompresses.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "blast.h"

static unsigned char *Data;
static long DataSize;

static unsigned rd16(long o) { return Data[o] | (Data[o + 1] << 8); }
static unsigned long rd32(long o) { return (unsigned long)Data[o] | ((unsigned long)Data[o + 1] << 8) | ((unsigned long)Data[o + 2] << 16) | ((unsigned long)Data[o + 3] << 24); }

struct InBuf { unsigned char *p; unsigned left; };
struct OutFile { FILE *f; unsigned long written; };

static unsigned in_fn(void *how, unsigned char **buf)
{
	struct InBuf *in = how;
	unsigned n = in->left;
	*buf = in->p;
	in->p += n;
	in->left = 0;
	return n;
}

static int out_fn(void *how, unsigned char *buf, unsigned len)
{
	struct OutFile *out = how;
	out->written += len;
	return fwrite(buf, 1, len, out->f) != len;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s ARCHIVE.Z OUTDIR [DATA_START]\n", argv[0]);
		return 2;
	}
	long data_start = argc > 3 ? strtol(argv[3], NULL, 0) : 255;
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END);
	DataSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	Data = malloc(DataSize);
	if (fread(Data, 1, DataSize, f) != (size_t)DataSize) { perror("read"); return 1; }
	fclose(f);

	if (rd32(0) != 0x8C655D13UL) { fprintf(stderr, "not an InstallShield 3 archive\n"); return 1; }
	long pos = 4 + 8 + 2 + 4 + 4 + 19;
	long toc = (long)rd32(pos); pos += 4 + 4;
	unsigned dir_count = rd16(pos);

	/* Directories: file count, chunk size, name. */
	char dir_names[64][256];
	unsigned dir_files[64];
	pos = toc;
	for (unsigned d = 0; d < dir_count && d < 64; d++) {
		dir_files[d] = rd16(pos);
		unsigned chunk = rd16(pos + 2);
		unsigned name_len = rd16(pos + 4);
		memcpy(dir_names[d], Data + pos + 6, name_len);
		dir_names[d][name_len] = '\0';
		pos += chunk;
	}

	/* Files, in directory order; data is laid out in the same order. */
	long accumulator = data_start;
	int errors = 0;
	for (unsigned d = 0; d < dir_count && d < 64; d++) {
		for (unsigned i = 0; i < dir_files[d]; i++) {
			unsigned long csize = rd32(pos + 7);
			unsigned chunk = rd16(pos + 7 + 4 + 12);
			unsigned name_len = Data[pos + 7 + 4 + 12 + 2 + 4];
			char name[256];
			memcpy(name, Data + pos + 30, name_len);
			name[name_len] = '\0';
			pos += chunk;

			char path[1024];
			snprintf(path, sizeof(path), "%s/%s", argv[2], name);
			for (char *p = path + strlen(argv[2]) + 1; *p; p++) *p = (char)toupper((unsigned char)*p);
			struct OutFile out = { fopen(path, "wb"), 0 };
			if (!out.f) { perror(path); return 1; }
			struct InBuf in = { Data + accumulator, (unsigned)csize };
			int rc = blast(in_fn, &in, out_fn, &out, NULL, NULL);
			fclose(out.f);
			printf("%-8s %-14s %9lu -> %9lu %s\n", dir_names[d][0] ? dir_names[d] : "(root)", name, csize, out.written, rc ? "ERROR" : "");
			if (rc) errors++;
			accumulator += (long)csize;
		}
	}
	return errors ? 1 : 0;
}
