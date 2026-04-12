/*
 * Standalone PCX decode (ZSoft 8bpp, 1 plane, RLE) for Atari ST test suite.
 */

#include "st_pcx_minimal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BIG_ENDIAN
static inline short st_swap16(short v)
{
	return (short)(((unsigned short)(v) << 8) | ((unsigned short)(v) >> 8));
}
#else
static inline short st_swap16(short v) { return v; }
#endif

#pragma pack(push, 1)
struct StPcxHdr {
	unsigned char id, version, encoding, bits_per_pixel;
	short x, y, x_end, y_end;
	short hres, vres;
	unsigned char ega[48];
	unsigned char reserved, planes;
	short bytes_per_line;
	short palette_type;
	unsigned char filler[58];
};
#pragma pack(pop)

struct StPcxMemRdr {
	const unsigned char *p;
	const unsigned char *end;
};

static int st_pcx_getc_mem(void *opaque)
{
	StPcxMemRdr *m = (StPcxMemRdr *)opaque;
	if (m->p >= m->end)
		return EOF;
	return (int)*m->p++;
}

static int st_pcx_decode_rle(unsigned char *buf, int width, int height, int row_stride, int BufferSize,
		int (*next_byte)(void *), void *opaque)
{
	for (int j = 0; j < height; j++) {
		long scan_pos = (long)j * row_stride;
		int index = 0;
		do {
			int c = next_byte(opaque);
			if (c == EOF)
				return -6;
			unsigned char byte = (unsigned char)c;
			unsigned int runcount;
			unsigned char runvalue;
			if ((byte & 0xC0) == 0xC0) {
				runcount = byte & 0x3F;
				int v = next_byte(opaque);
				if (v == EOF)
					return -7;
				runvalue = (unsigned char)v;
			} else {
				runcount = 1;
				runvalue = byte;
			}
			for (; runcount && index < BufferSize; runcount--, index++) {
				if (index < width)
					buf[scan_pos + index] = runvalue;
			}
		} while (index < BufferSize);
	}
	return 0;
}

static int st_pcx_palette_from_tail(const unsigned char *tail_769, unsigned char *palette768)
{
	if (tail_769[0] != 0x0c)
		return -9;
	memcpy(palette768, tail_769 + 1, 768);
	for (int i = 0; i < 256; i++) {
		palette768[i * 3 + 0] = (unsigned char)(palette768[i * 3 + 0] >> 2);
		palette768[i * 3 + 1] = (unsigned char)(palette768[i * 3 + 1] >> 2);
		palette768[i * 3 + 2] = (unsigned char)(palette768[i * 3 + 2] >> 2);
	}
	return 0;
}

static int st_pcx_getc_file(void *opaque)
{
	return fgetc((FILE *)opaque);
}

static int st_pcx_parse_header(const unsigned char *hdr128, StPcxHdr *out_h, int *out_width, int *out_height,
		int *out_row_stride)
{
	memcpy(out_h, hdr128, sizeof(StPcxHdr));
	StPcxHdr h = *out_h;
	if (h.id != 10 || h.bits_per_pixel != 8)
		return -3;

	h.x = st_swap16(h.x);
	h.y = st_swap16(h.y);
	h.x_end = st_swap16(h.x_end);
	h.y_end = st_swap16(h.y_end);
	h.bytes_per_line = st_swap16(h.bytes_per_line);

	int width = (int)h.x_end - (int)h.x + 1;
	int height = (int)h.y_end - (int)h.y + 1;
	if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
		return -4;
	if (h.bytes_per_line <= 0)
		h.bytes_per_line = (short)width;

	unsigned char planes = (unsigned char)h.planes;
	int bpl = (int)h.bytes_per_line;
	if (planes == 1 && bpl > 0 && bpl < width)
		width = bpl;

	int row_stride = width > bpl ? width : bpl;
	*out_h = h;
	*out_width = width;
	*out_height = height;
	*out_row_stride = row_stride;
	return 0;
}

int st_pcx_load_from_memory(const unsigned char *file_bytes, size_t file_len, unsigned char **out_buf,
		int *out_w, int *out_h, int *out_row_stride, unsigned char *palette768)
{
	if (!file_bytes || file_len < 128u + 769u || !out_buf)
		return -2;

	StPcxHdr h;
	int width, height, row_stride;
	int prc = st_pcx_parse_header(file_bytes, &h, &width, &height, &row_stride);
	if (prc != 0)
		return prc;

	unsigned char *buf = (unsigned char *)malloc((size_t)row_stride * (size_t)height);
	if (!buf)
		return -5;
	memset(buf, 0, (size_t)row_stride * (size_t)height);

	StPcxMemRdr mr;
	mr.p = file_bytes + 128;
	mr.end = file_bytes + file_len - 769;
	int dr = st_pcx_decode_rle(buf, width, height, row_stride, (int)h.bytes_per_line, st_pcx_getc_mem, &mr);
	if (dr != 0) {
		free(buf);
		return dr;
	}

	if (palette768) {
		int pe = st_pcx_palette_from_tail(file_bytes + file_len - 769, palette768);
		if (pe != 0) {
			free(buf);
			return pe;
		}
	}

	if (*out_buf)
		free(*out_buf);
	*out_buf = buf;
	if (out_w)
		*out_w = width;
	if (out_h)
		*out_h = height;
	if (out_row_stride)
		*out_row_stride = row_stride;
	return 0;
}

int st_pcx_load(const char *path, unsigned char **out_buf, int *out_w, int *out_h, int *out_row_stride,
		unsigned char *palette768)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;

	unsigned char hdr[128];
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fclose(f);
		return -2;
	}

	StPcxHdr h;
	int width, height, row_stride;
	int prc = st_pcx_parse_header(hdr, &h, &width, &height, &row_stride);
	if (prc != 0) {
		fclose(f);
		return prc;
	}

	unsigned char *buf = (unsigned char *)malloc((size_t)row_stride * (size_t)height);
	if (!buf) {
		fclose(f);
		return -5;
	}
	memset(buf, 0, (size_t)row_stride * (size_t)height);

	int dr = st_pcx_decode_rle(buf, width, height, row_stride, (int)h.bytes_per_line, st_pcx_getc_file, f);
	if (dr != 0) {
		free(buf);
		fclose(f);
		return dr;
	}

	if (palette768) {
		if (fseek(f, -769L, SEEK_END) != 0) {
			free(buf);
			fclose(f);
			return -8;
		}
		unsigned char tail[769];
		if (fread(tail, 1, sizeof(tail), f) != sizeof(tail)) {
			free(buf);
			fclose(f);
			return -10;
		}
		if (st_pcx_palette_from_tail(tail, palette768) != 0) {
			free(buf);
			fclose(f);
			return -9;
		}
	}

	fclose(f);

	if (!out_buf) {
		free(buf);
		return -11;
	}
	if (*out_buf)
		free(*out_buf);
	*out_buf = buf;
	if (out_w)
		*out_w = width;
	if (out_h)
		*out_h = height;
	if (out_row_stride)
		*out_row_stride = row_stride;
	return 0;
}
