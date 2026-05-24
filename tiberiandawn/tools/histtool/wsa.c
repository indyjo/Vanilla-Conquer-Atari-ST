#include "wsa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WSA_HEADER_SIZE 14
#define WSA_DELTA_UNDERSIZE_BIAS 37

static unsigned short read_le16(const unsigned char *p)
{
	return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}

static unsigned long read_le32(const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
	       ((unsigned long)p[3] << 24);
}

static unsigned short read_lcw_u16(const unsigned char *p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

/* Westwood LCW ("Format 80") decompression; matches ATARILIB/iff.cpp. */
static unsigned long lcw_uncompress(const unsigned char *source, unsigned char *dest, unsigned long length)
{
	const unsigned char *src = source;
	unsigned char *dst = dest;
	unsigned char *dst0 = dst;
	unsigned char *dst_end = dst + length;
	int relative_mode = 0;

	if (!source || !dest || length == 0)
		return 0;

	if (*src == 0) {
		relative_mode = 1;
		src++;
	}

	while (dst < dst_end) {
		unsigned char code = *src++;

		if ((code & 0x80) == 0) {
			unsigned long count = (unsigned long)((code & 0x70) >> 4) + 3UL;
			unsigned long pos = ((unsigned long)(code & 0x0F) << 8) | (unsigned long)(*src++);
			unsigned char *from = dst - (long)pos;
			unsigned long k;

			if (from < dst0)
				break;
			if (dst + count > dst_end)
				count = (unsigned long)(dst_end - dst);
			for (k = 0; k < count; k++)
				*dst++ = from[k];
			continue;
		}

		if ((code & 0x40) == 0) {
			unsigned long count = (unsigned long)(code & 0x3F);
			if (count == 0)
				break;
			if (dst + count > dst_end)
				count = (unsigned long)(dst_end - dst);
			memcpy(dst, src, count);
			dst += count;
			src += count;
			continue;
		}

		if (code == 0xFE) {
			unsigned long count = (unsigned long)read_lcw_u16(src);
			unsigned char value;
			src += 2;
			value = *src++;
			if (dst + count > dst_end)
				count = (unsigned long)(dst_end - dst);
			memset(dst, value, count);
			dst += count;
			continue;
		}

		if (code == 0xFF) {
			unsigned long count = (unsigned long)read_lcw_u16(src);
			unsigned short posw = read_lcw_u16(src);
			unsigned char *from;
			unsigned long k;

			src += 4;
			from = relative_mode ? (dst - (long)posw) : (dst0 + (long)posw);
			if (from < dst0)
				break;
			if (dst + count > dst_end)
				count = (unsigned long)(dst_end - dst);
			for (k = 0; k < count; k++)
				*dst++ = from[k];
			continue;
		}

		{
			unsigned long count = (unsigned long)(code & 0x3F) + 3UL;
			unsigned short posw = read_lcw_u16(src);
			unsigned char *from;
			unsigned long k;

			src += 2;
			from = relative_mode ? (dst - (long)posw) : (dst0 + (long)posw);
			if (from < dst0)
				break;
			if (dst + count > dst_end)
				count = (unsigned long)(dst_end - dst);
			for (k = 0; k < count; k++)
				*dst++ = from[k];
		}
	}

	return (unsigned long)(dst - dst0);
}

/* Viewport-style XOR-delta decode (matches wsa.cpp Apply_XOR_Delta_To_Page_Or_Viewport). */
static int apply_xor_delta_viewport(unsigned char *target, int width, int height, const unsigned char *delta,
	const unsigned char *delta_end, int copy_mode)
{
	unsigned char *dst = target;
	const unsigned char *src = delta;
	int col = 0;
	unsigned long frame_bytes = (unsigned long)width * (unsigned long)height;
	unsigned char *frame_end = target + frame_bytes;

	while (1) {
		unsigned int code;

		if (src >= delta_end)
			return -1;
		code = (unsigned int)*src++;

		if (code > 0 && code < 128) {
			unsigned int count = code;
			while (count--) {
				unsigned char v;
				if (src >= delta_end || dst >= frame_end)
					return -1;
				v = *src++;
				if (copy_mode)
					*dst = v;
				else
					*dst ^= v;
				dst++;
				col++;
				if (col == width) {
					dst -= width;
					col = 0;
					dst += width;
				}
			}
			continue;
		}

		if (code == 0) {
			unsigned int count;
			unsigned char v;
			if (src + 1 >= delta_end)
				return -1;
			count = (unsigned int)*src++;
			v = (unsigned char)*src++;
			while (count--) {
				if (dst >= frame_end)
					return -1;
				if (copy_mode)
					*dst = v;
				else
					*dst ^= v;
				dst++;
				col++;
				if (col == width) {
					dst -= width;
					col = 0;
					dst += width;
				}
			}
			continue;
		}

		if (code > 128) {
			unsigned int skip = code - 128U;
			dst -= col;
			col += (int)skip;
			while (col >= width) {
				col -= width;
				dst += width;
			}
			dst += col;
			if (dst > frame_end)
				return -1;
			continue;
		}

		{
			unsigned int word_code;
			if (src + 1 >= delta_end)
				return -1;
			word_code = (unsigned int)read_le16(src);
			src += 2;

			if (word_code == 0)
				break;

			if (word_code < 0x8000u) {
				unsigned int skip = word_code;
				dst -= col;
				col += (int)skip;
				while (col >= width) {
					col -= width;
					dst += width;
				}
				dst += col;
				if (dst > frame_end)
					return -1;
				continue;
			}

			{
				unsigned int x = word_code - 0x8000u;
				if ((x & 0x4000u) == 0u) {
					unsigned int count = x;
					while (count--) {
						unsigned char v;
						if (src >= delta_end || dst >= frame_end)
							return -1;
						v = *src++;
						if (copy_mode)
							*dst = v;
						else
							*dst ^= v;
						dst++;
						col++;
						if (col == width) {
							dst -= width;
							col = 0;
							dst += width;
						}
					}
				} else {
					unsigned int count = x - 0x4000u;
					unsigned char v;
					if (src >= delta_end)
						return -1;
					v = (unsigned char)*src++;
					while (count--) {
						if (dst >= frame_end)
							return -1;
						if (copy_mode)
							*dst = v;
						else
							*dst ^= v;
						dst++;
						col++;
						if (col == width) {
							dst -= width;
							col = 0;
							dst += width;
						}
					}
				}
			}
		}
	}

	return 0;
}

static int wsa_apply_xor_delta(unsigned char *frame_buf, unsigned short width, unsigned short height,
	unsigned char *delta_buf, unsigned long lcw_cap, const unsigned char *compressed, int copy_mode)
{
	unsigned long delta_len;
	const unsigned char *delta_end;

	delta_len = lcw_uncompress(compressed, delta_buf, lcw_cap);
	delta_end = delta_buf + delta_len;
	return apply_xor_delta_viewport(frame_buf, (int)width, (int)height, delta_buf, delta_end, copy_mode);
}

static void hist_accumulate_frame(const unsigned char *frame, unsigned long frame_bytes, HistCounts *counts)
{
	unsigned long i;
	for (i = 0; i < frame_bytes; i++)
		counts->counts[frame[i]]++;
}

static int wsa_decode_frame(const unsigned char *raw, long file_size, const char *path, unsigned short total_frames,
	unsigned short width, unsigned short height, unsigned short largest_frame_size, unsigned short flags,
	int frame_index, unsigned char *frame_buf, unsigned char *delta_buf)
{
	const unsigned char *offsets;
	unsigned long table_bytes;
	unsigned long palette_adj;
	unsigned long frame_bytes;
	int frame0_on_page;
	unsigned long f;
	unsigned long off;
	unsigned long size;

	if (frame_index < 0 || frame_index >= (int)total_frames) {
		fprintf(stderr, "error: internal WSA frame index out of range\n");
		return -1;
	}

	table_bytes = (unsigned long)(total_frames + 2) * 4UL;
	if (14L + (long)table_bytes > file_size) {
		fprintf(stderr, "error: WSA offset table extends past end of file\n");
		return -1;
	}

	offsets = raw + WSA_HEADER_SIZE;
	palette_adj = (flags & 1u) ? 768UL : 0UL;
	frame_bytes = (unsigned long)width * (unsigned long)height;
	frame0_on_page = (read_le32(offsets) == 0);

	memset(frame_buf, 0, frame_bytes);

	if (!frame0_on_page) {
		off = 14UL + table_bytes + palette_adj;
		size = read_le32(offsets + 4) - read_le32(offsets);
		if (off + size > (unsigned long)file_size) {
			fprintf(stderr, "error: %s: WSA frame 0 data extends past end of file\n", path);
			return -1;
		}
		if (flags & 2u) {
			if (wsa_apply_xor_delta(frame_buf, width, height, delta_buf, largest_frame_size, raw + off, 0) != 0) {
				fprintf(stderr, "error: %s: failed to decode WSA frame 0\n", path);
				return -1;
			}
		} else {
			lcw_uncompress(raw + off, frame_buf, frame_bytes);
		}
	}

	for (f = 1; f <= (unsigned long)frame_index; f++) {
		off = read_le32(offsets + (f * 4)) + palette_adj;
		size = read_le32(offsets + ((f + 1) * 4)) - read_le32(offsets + (f * 4));
		if (size == 0 || off + size > (unsigned long)file_size) {
			fprintf(stderr, "error: %s: WSA frame %lu data invalid\n", path, f);
			return -1;
		}
		if (wsa_apply_xor_delta(frame_buf, width, height, delta_buf, largest_frame_size, raw + off, 0) != 0) {
			fprintf(stderr, "error: %s: failed to decode WSA frame %lu\n", path, f);
			return -1;
		}
	}

	return 0;
}

long long wsa_hist_accumulate(const char *path, HistCounts *counts)
{
	FILE *f;
	unsigned char header[WSA_HEADER_SIZE];
	unsigned short total_frames;
	unsigned short width;
	unsigned short height;
	unsigned short largest_frame_size;
	unsigned short flags;
	unsigned char *raw = NULL;
	unsigned char *frame_buf = NULL;
	unsigned char *delta_buf = NULL;
	unsigned long delta_cap;
	long file_size;
	long long pixels = 0;
	int frame;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return -1;
	}

	if (fseek(f, 0L, SEEK_END) != 0) {
		fprintf(stderr, "error: cannot seek %s\n", path);
		fclose(f);
		return -1;
	}
	file_size = ftell(f);
	if (file_size < WSA_HEADER_SIZE) {
		fprintf(stderr, "error: %s: file too short for WSA\n", path);
		fclose(f);
		return -1;
	}
	if (fseek(f, 0L, SEEK_SET) != 0) {
		fprintf(stderr, "error: cannot rewind %s\n", path);
		fclose(f);
		return -1;
	}
	if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
		fprintf(stderr, "error: %s: short read (WSA header)\n", path);
		fclose(f);
		return -1;
	}

	total_frames = read_le16(header + 0);
	width = read_le16(header + 6);
	height = read_le16(header + 8);
	largest_frame_size = read_le16(header + 10);
	flags = read_le16(header + 12);

	if (total_frames == 0) {
		fprintf(stderr, "error: %s: WSA has no frames\n", path);
		fclose(f);
		return -1;
	}
	if (width == 0 || height == 0) {
		fprintf(stderr, "error: %s: invalid WSA dimensions\n", path);
		fclose(f);
		return -1;
	}
	if (largest_frame_size == 0) {
		fprintf(stderr, "error: %s: invalid WSA largest_frame_size\n", path);
		fclose(f);
		return -1;
	}

	raw = (unsigned char *)malloc((size_t)file_size);
	if (!raw) {
		fprintf(stderr, "error: out of memory\n");
		fclose(f);
		return -1;
	}
	rewind(f);
	if (fread(raw, 1, (size_t)file_size, f) != (size_t)file_size) {
		fprintf(stderr, "error: %s: short read\n", path);
		free(raw);
		fclose(f);
		return -1;
	}
	fclose(f);

	delta_cap = (unsigned long)largest_frame_size + WSA_DELTA_UNDERSIZE_BIAS;
	frame_buf = (unsigned char *)malloc((size_t)width * (size_t)height);
	delta_buf = (unsigned char *)malloc((size_t)delta_cap);
	if (!frame_buf || !delta_buf) {
		fprintf(stderr, "error: out of memory\n");
		free(raw);
		free(frame_buf);
		free(delta_buf);
		return -1;
	}

	for (frame = 0; frame < (int)total_frames; frame++) {
		unsigned long frame_bytes = (unsigned long)width * (unsigned long)height;
		if (wsa_decode_frame(raw, file_size, path, total_frames, width, height, largest_frame_size, flags, frame,
				frame_buf, delta_buf) != 0) {
			free(raw);
			free(frame_buf);
			free(delta_buf);
			return -1;
		}
		hist_accumulate_frame(frame_buf, frame_bytes, counts);
		pixels += (long long)frame_bytes;
	}

	free(raw);
	free(frame_buf);
	free(delta_buf);
	return pixels;
}
