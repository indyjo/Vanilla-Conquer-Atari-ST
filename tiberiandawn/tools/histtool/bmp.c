#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>

#define BI_RGB   0
#define BI_RLE8  1

static unsigned read_u16_le(FILE *f)
{
	unsigned char b[2];
	if (fread(b, 1, 2, f) != 2)
		return 0;
	return (unsigned)b[0] | ((unsigned)b[1] << 8);
}

static unsigned long read_u32_le(FILE *f)
{
	unsigned char b[4];
	if (fread(b, 1, 4, f) != 4)
		return 0;
	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) |
	       ((unsigned long)b[3] << 24);
}

static int read_u8(FILE *f, unsigned char *out)
{
	return fread(out, 1, 1, f) == 1;
}

static void hist_count_pixel(HistCounts *counts, unsigned char idx, long x, long y, long width, long height,
	long long *pixels)
{
	if (x >= 0 && x < width && y >= 0 && y < height) {
		counts->counts[idx]++;
		(*pixels)++;
	}
}

static long long bmp_hist_rle8(FILE *f, const char *path, long width, long height, HistCounts *counts)
{
	long x = 0;
	long y = 0;
	long long pixels = 0;
	unsigned char b0;
	unsigned char b1;
	int done = 0;

	while (!done) {
		if (!read_u8(f, &b0)) {
			if (feof(f))
				break;
			fprintf(stderr, "error: %s: short read in RLE8 data\n", path);
			return -1;
		}

		if (b0 != 0) {
			if (!read_u8(f, &b1)) {
				fprintf(stderr, "error: %s: short read in RLE8 run\n", path);
				return -1;
			}
			while (b0-- > 0) {
				hist_count_pixel(counts, b1, x, y, width, height, &pixels);
				x++;
			}
			continue;
		}

		if (!read_u8(f, &b1)) {
			fprintf(stderr, "error: %s: short read in RLE8 escape\n", path);
			return -1;
		}

		if (b1 == 0) {
			x = 0;
			y++;
			if (y >= height)
				done = 1;
		} else if (b1 == 1) {
			done = 1;
		} else if (b1 == 2) {
			unsigned char dx;
			unsigned char dy;
			if (!read_u8(f, &dx) || !read_u8(f, &dy)) {
				fprintf(stderr, "error: %s: short read in RLE8 delta\n", path);
				return -1;
			}
			x += (long)dx;
			y += (long)dy;
		} else {
			unsigned run = b1;
			while (run-- > 0) {
				unsigned char idx;
				if (!read_u8(f, &idx)) {
					fprintf(stderr, "error: %s: short read in RLE8 absolute run\n", path);
					return -1;
				}
				hist_count_pixel(counts, idx, x, y, width, height, &pixels);
				x++;
			}
			if (b1 & 1) {
				unsigned char pad;
				if (!read_u8(f, &pad)) {
					fprintf(stderr, "error: %s: short read in RLE8 pad byte\n", path);
					return -1;
				}
			}
		}
	}

	return pixels;
}

static long long bmp_hist_uncompressed(FILE *f, const char *path, long width, long height, HistCounts *counts)
{
	unsigned row_bytes = (unsigned)((width + 3) & ~3);
	unsigned char *row;
	long y;
	long long pixels = 0;

	row = (unsigned char *)malloc(row_bytes);
	if (!row) {
		fprintf(stderr, "error: out of memory\n");
		return -1;
	}

	for (y = 0; y < height; y++) {
		long x;
		if (fread(row, 1, row_bytes, f) != row_bytes) {
			fprintf(stderr, "error: %s: short read at row %ld\n", path, y);
			free(row);
			return -1;
		}
		for (x = 0; x < width; x++) {
			counts->counts[row[x]]++;
			pixels++;
		}
	}

	free(row);
	return pixels;
}

long long bmp_hist_accumulate(const char *path, HistCounts *counts)
{
	FILE *f;
	unsigned long off_bits;
	long width;
	long height;
	unsigned planes;
	unsigned bpp;
	unsigned long compression;
	long long pixels = 0;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return -1;
	}

	if (read_u16_le(f) != 0x4D42) {
		fprintf(stderr, "error: %s: not a BMP (bad signature)\n", path);
		fclose(f);
		return -1;
	}
	(void)read_u32_le(f);
	(void)read_u16_le(f);
	(void)read_u16_le(f);
	off_bits = read_u32_le(f);

	if (read_u32_le(f) != 40) {
		fprintf(stderr, "error: %s: unsupported DIB header size\n", path);
		fclose(f);
		return -1;
	}
	width = (long)read_u32_le(f);
	height = (long)read_u32_le(f);
	planes = read_u16_le(f);
	bpp = read_u16_le(f);
	compression = read_u32_le(f);

	if (width <= 0 || height == 0 || height == (long)0x80000000L) {
		fprintf(stderr, "error: %s: invalid dimensions\n", path);
		fclose(f);
		return -1;
	}

	if (height < 0)
		height = -height;

	if (planes != 1 || bpp != 8) {
		fprintf(stderr, "error: %s: %u bpp not supported (8-bit indexed only)\n", path, bpp);
		fclose(f);
		return -1;
	}

	if (compression != BI_RGB && compression != BI_RLE8) {
		fprintf(stderr, "error: %s: compression %lu not supported (BI_RGB or BI_RLE8 only)\n", path,
			compression);
		fclose(f);
		return -1;
	}

	if (fseek(f, (long)off_bits, SEEK_SET) != 0) {
		fprintf(stderr, "error: %s: seek to pixel data failed\n", path);
		fclose(f);
		return -1;
	}

	if (compression == BI_RLE8)
		pixels = bmp_hist_rle8(f, path, width, height, counts);
	else
		pixels = bmp_hist_uncompressed(f, path, width, height, counts);

	fclose(f);
	return pixels;
}
