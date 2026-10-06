#include "cps.h"
#include "wsa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* On-disk CPS: uint16 file size, then an 8-byte CompHeader, then `skip` bytes
 * (usually a 768-byte VGA palette), then the compressed picture.
 * CompHeader: method, pad, uint32 uncompressed size, uint16 skip.
 * method 4 is LCW. Matches Load_Uncompress / Uncompress_Data. */

#define CPS_METHOD_NONE 0
#define CPS_METHOD_LCW 4
#define CPS_MAX_PIXELS (1024UL * 1024UL)

static unsigned short read_le16(const unsigned char *p)
{
	return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}

static unsigned long read_le32(const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
	       ((unsigned long)p[3] << 24);
}

long long cps_hist_accumulate(const char *path, HistCounts *counts)
{
	FILE *f;
	long file_size;
	unsigned char *raw = NULL;
	unsigned char *pixels = NULL;
	unsigned short declared;
	unsigned char method;
	unsigned long uncomp;
	unsigned short skip;
	unsigned long data_off;
	unsigned long got;
	unsigned long i;
	long long n = -1;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 10) {
		fprintf(stderr, "error: %s: file too short for CPS\n", path);
		fclose(f);
		return -1;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fprintf(stderr, "error: %s: seek failed\n", path);
		fclose(f);
		return -1;
	}
	raw = (unsigned char *)malloc((size_t)file_size);
	if (!raw || fread(raw, 1, (size_t)file_size, f) != (size_t)file_size) {
		fprintf(stderr, "error: %s: short read\n", path);
		free(raw);
		fclose(f);
		return -1;
	}
	fclose(f);

	declared = read_le16(raw);
	method = raw[2];
	uncomp = read_le32(raw + 4);
	skip = read_le16(raw + 8);
	data_off = 10UL + (unsigned long)skip;

	if (uncomp == 0 || uncomp > CPS_MAX_PIXELS) {
		fprintf(stderr, "error: %s: bad CPS uncompressed size %lu\n", path, uncomp);
		goto done;
	}
	if (data_off >= (unsigned long)file_size) {
		fprintf(stderr, "error: %s: CPS picture starts past end of file (skip=%u)\n", path, skip);
		goto done;
	}

	pixels = (unsigned char *)malloc(uncomp);
	if (!pixels) {
		fprintf(stderr, "error: %s: out of memory (%lu pixels)\n", path, uncomp);
		goto done;
	}

	if (method == CPS_METHOD_NONE) {
		if (data_off + uncomp > (unsigned long)file_size) {
			fprintf(stderr, "error: %s: uncompressed CPS extends past end of file\n", path);
			goto done;
		}
		memcpy(pixels, raw + data_off, uncomp);
		got = uncomp;
	} else if (method == CPS_METHOD_LCW) {
		got = lcw_uncompress(raw + data_off, pixels, uncomp);
		if (got != uncomp) {
			fprintf(stderr, "error: %s: LCW decode wrote %lu bytes, expected %lu\n", path, got, uncomp);
			goto done;
		}
	} else {
		fprintf(stderr, "error: %s: unsupported CPS compression method %u\n", path, method);
		goto done;
	}

	(void)declared;
	for (i = 0; i < uncomp; i++)
		counts->counts[pixels[i]]++;
	n = (long long)uncomp;

done:
	free(pixels);
	free(raw);
	return n;
}
