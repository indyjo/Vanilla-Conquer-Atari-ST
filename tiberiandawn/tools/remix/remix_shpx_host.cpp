/*
 * SHPX conversion for eligible MIX KeyFrame SHPs (host remix + remix-web WASM).
 * Eligible: CONQUER.MIX, TEMPERAT.MIX, DESERT.MIX, WINTER.MIX.
 */

#include "remix_shpx.h"

#include "keyframe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
	SHPX_MAGIC_SIZE = 4,
	SHPX_KF_HEADER_SIZE = 14,
	SHPX_PREFIX_SIZE = 38,
	SHPX_FRAME_SLOT_SIZE = 8,
	KF_HEADER_SIZE = 14,
	SHPX_KF_DELTA_FLAG = 0x20u
};

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_be16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)((v >> 8) & 0xFFu);
	p[1] = (unsigned char)(v & 0xFFu);
}

static void write_be32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)((v >> 24) & 0xFFu);
	p[1] = (unsigned char)((v >> 16) & 0xFFu);
	p[2] = (unsigned char)((v >> 8) & 0xFFu);
	p[3] = (unsigned char)(v & 0xFFu);
}

static int looks_like_shapeblock(const unsigned char *data, size_t len)
{
	uint16_t count;
	uint32_t first_off;

	if (len < 6)
		return 0;
	count = read_le16(data);
	if (count == 0 || count > 4096)
		return 0;
	if ((size_t)2 + (size_t)count * 4u + 4u > len)
		return 0;
	first_off = read_le32(data + 2);
	if (first_off < (uint32_t)(2 + count * 4) || first_off >= len)
		return 0;
	return 1;
}

uint16_t remix_shpx_default_pool_id(const char *mix_basename)
{
	if (!mix_basename)
		return 0;
	if (strcasecmp(mix_basename, "CONQUER.MIX") == 0)
		return 0x0001u;
	if (strcasecmp(mix_basename, "TEMPERAT.MIX") == 0)
		return 0x0002u;
	if (strcasecmp(mix_basename, "DESERT.MIX") == 0)
		return 0x0003u;
	if (strcasecmp(mix_basename, "WINTER.MIX") == 0)
		return 0x0004u;
	return 0;
}

int remix_shpx_is_eligible(const char *mix_basename)
{
	return remix_shpx_default_pool_id(mix_basename) != 0;
}

int remix_shpx_format_pool_name(uint16_t pool_id, char *out, size_t out_cap)
{
	if (!out || out_cap < 13u)
		return 0;
	if (snprintf(out, out_cap, "pool%04x.bin", (unsigned)pool_id) >= (int)out_cap)
		return 0;
	return 1;
}

int remix_is_keyframe_shp(const unsigned char *data, size_t len)
{
	uint16_t frames;
	size_t tail_start;
	uint32_t first_word;
	uint32_t low;

	if (len < KF_HEADER_SIZE + SHPX_FRAME_SLOT_SIZE)
		return 0;
	if (len >= SHPX_MAGIC_SIZE && data[0] == 'S' && data[1] == 'H' && data[2] == 'P'
	    && data[3] == 'X')
		return 0;
	if (looks_like_shapeblock(data, len))
		return 0;

	frames = read_le16(data);
	if (frames == 0 || frames > 4096)
		return 0;
	tail_start = (size_t)KF_HEADER_SIZE + (size_t)frames * SHPX_FRAME_SLOT_SIZE;
	if (tail_start >= len)
		return 0;

	first_word = read_le32(data + KF_HEADER_SIZE);
	low = first_word & 0x00FFFFFFu;
	if (low < tail_start || low >= len)
		return 0;
	return 1;
}

void remix_shpx_pool_init(RemixShpxPool *pool, uint16_t pool_id)
{
	memset(pool, 0, sizeof(*pool));
	pool->pool_id = pool_id;
}

void remix_shpx_pool_free(RemixShpxPool *pool)
{
	free(pool->data);
	pool->data = NULL;
	pool->size = 0;
	pool->cap = 0;
}

static int pool_ensure(RemixShpxPool *pool, size_t need)
{
	size_t new_cap;
	uint8_t *p;

	if (need <= pool->cap)
		return 1;
	new_cap = pool->cap ? pool->cap : 65536u;
	while (new_cap < need)
		new_cap *= 2u;
	p = (uint8_t *)realloc(pool->data, new_cap);
	if (!p)
		return 0;
	pool->data = p;
	pool->cap = new_cap;
	return 1;
}

static int pool_append_byte(RemixShpxPool *pool, uint8_t b)
{
	if (!pool_ensure(pool, pool->size + 1))
		return 0;
	pool->data[pool->size++] = b;
	return 1;
}

static int pool_append(RemixShpxPool *pool, const void *src, size_t n)
{
	if (n == 0)
		return 1;
	if (!pool_ensure(pool, pool->size + n))
		return 0;
	memcpy(pool->data + pool->size, src, n);
	pool->size += n;
	return 1;
}

static int table_word_needs_rebase(
    const unsigned char *blob, size_t blob_len, size_t tail_start, size_t byte_off)
{
	uint32_t w0;
	size_t slot_base;

	(void)tail_start;
	if (byte_off < KF_HEADER_SIZE || byte_off + 4 > blob_len)
		return 0;
	if (((byte_off - KF_HEADER_SIZE) % 4) != 0)
		return 0;

	slot_base = byte_off - ((byte_off - KF_HEADER_SIZE) % SHPX_FRAME_SLOT_SIZE);
	if (slot_base < KF_HEADER_SIZE)
		return 0;

	if ((byte_off - slot_base) == 4) {
		w0 = read_le32(blob + slot_base);
		if ((w0 >> 24) & SHPX_KF_DELTA_FLAG)
			return 0;
	}
	return 1;
}

static void convert_frame_table(
    unsigned char *dst, const unsigned char *src, uint16_t frames, size_t tail_start,
    size_t blob_len)
{
	size_t table_bytes = (size_t)frames * SHPX_FRAME_SLOT_SIZE;
	size_t k;

	memcpy(dst, src + KF_HEADER_SIZE, table_bytes);
	for (k = 0; k < (size_t)frames * 2u; ++k) {
		size_t byte_off = KF_HEADER_SIZE + k * 4u;
		uint32_t word = read_le32(src + byte_off);
		uint32_t flags = word >> 24;
		uint32_t low = word & 0x00FFFFFFu;

		if (table_word_needs_rebase(src, blob_len, tail_start, byte_off)
		    && low >= tail_start && low < blob_len)
			low -= (uint32_t)tail_start;

		word = (flags << 24) | low;
		write_be32(dst + byte_off - KF_HEADER_SIZE, word);
	}
}

static void compute_clip(
    const unsigned char *pixels, unsigned width, unsigned height, uint16_t *cx, uint16_t *cy,
    uint16_t *cw, uint16_t *ch)
{
	unsigned x;
	unsigned y;
	int minx = (int)width;
	int miny = (int)height;
	int maxx = -1;
	int maxy = -1;

	for (y = 0; y < height; ++y) {
		const unsigned char *row = pixels + (size_t)y * (size_t)width;
		for (x = 0; x < width; ++x) {
			if (row[x] == 0)
				continue;
			if ((int)x < minx)
				minx = (int)x;
			if ((int)y < miny)
				miny = (int)y;
			if ((int)x > maxx)
				maxx = (int)x;
			if ((int)y > maxy)
				maxy = (int)y;
		}
	}

	if (maxx < 0) {
		*cx = 0;
		*cy = 0;
		*cw = 0;
		*ch = 0;
		return;
	}
	*cx = (uint16_t)minx;
	*cy = (uint16_t)miny;
	*cw = (uint16_t)(maxx - minx + 1);
	*ch = (uint16_t)(maxy - miny + 1);
}

static int build_clips(
    const unsigned char *blob, size_t blob_len, uint16_t frames, unsigned char *clip_out,
    const RemixShpxConvertOpts *opts)
{
	unsigned short f;
	unsigned short width = Get_Build_Frame_Width(blob);
	unsigned short height = Get_Build_Frame_Height(blob);
	unsigned long buf_bytes = Get_Build_Frame_BufferBytes(blob);
	unsigned char *buf;
	const unsigned char *frame_blob = blob;
	size_t frame_blob_len = blob_len;
	unsigned empty = 0;
	unsigned decode_fail = 0;
	uint16_t max_cw = 0;
	uint16_t max_ch = 0;
	int rc = 0;

	if (width == 0 || height == 0 || buf_bytes == 0)
		return 0;

	buf = (unsigned char *)malloc(buf_bytes);
	if (!buf)
		return 0;

	if (opts && opts->verbose) {
		fprintf(stderr, "shpx %08X: clips %u frames, logical %ux%u\n", (unsigned)opts->entry_crc,
		    (unsigned)frames, (unsigned)width, (unsigned)height);
	}

	for (f = 0; f < frames; ++f) {
		uint16_t cx;
		uint16_t cy;
		uint16_t cw;
		uint16_t ch;
		unsigned char *row = clip_out + (size_t)f * 8u;
		int decoded;

		if (!Build_Frame(frame_blob, f, buf, frame_blob_len)) {
			cx = cy = cw = ch = 0;
			decoded = 0;
			++decode_fail;
		} else {
			compute_clip(buf, width, height, &cx, &cy, &cw, &ch);
			decoded = 1;
		}

		if (cw == 0 || ch == 0)
			++empty;
		if (cw > max_cw)
			max_cw = cw;
		if (ch > max_ch)
			max_ch = ch;

		if (opts && opts->verbose) {
			if (!decoded)
				fprintf(stderr, "  fr %3u: decode fail -> empty clip\n", (unsigned)f);
			else if (cw == 0)
				fprintf(stderr, "  fr %3u: clip %u,%u 0x0 (transparent)\n", (unsigned)f,
				    (unsigned)cx, (unsigned)cy);
			else
				fprintf(stderr, "  fr %3u: clip %u,%u %ux%u\n", (unsigned)f, (unsigned)cx,
				    (unsigned)cy, (unsigned)cw, (unsigned)ch);
		}

		write_be16(row + 0, cx);
		write_be16(row + 2, cy);
		write_be16(row + 4, cw);
		write_be16(row + 6, ch);
	}

	if (opts && opts->verbose) {
		fprintf(stderr, "shpx %08X: clip summary - empty %u, decode fail %u, max tight %ux%u\n",
		    (unsigned)opts->entry_crc, empty, decode_fail, (unsigned)max_cw, (unsigned)max_ch);
	}

	rc = 1;
	free(buf);
	return rc;
}

static void write_kf_header_be(unsigned char *dst, const unsigned char *src)
{
	unsigned i;
	for (i = 0; i < SHPX_KF_HEADER_SIZE; i += 2)
		write_be16(dst + i, read_le16(src + i));
}

int remix_shpx_convert(
    const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len,
    RemixShpxPool *pool, const RemixShpxConvertOpts *opts)
{
	uint16_t frames;
	int16_t flags;
	size_t tail_start;
	size_t tail_size;
	size_t frame_table_bytes;
	size_t clip_table_bytes;
	size_t meta_size;
	uint32_t pool_begin;
	uint32_t pool_size;
	unsigned char *out;
	size_t frame_off;
	size_t clip_off;

	if (!in)
		return 0;

	if (!remix_is_keyframe_shp(in, in_len))
		return -1;

	if (!pool || !out_buf || !out_len)
		return 0;

	*out_buf = NULL;
	*out_len = 0;

	frames = read_le16(in);
	flags = (int16_t)read_le16(in + 12);
	if (flags & 1)
		return -1;

	tail_start = (size_t)KF_HEADER_SIZE + (size_t)frames * SHPX_FRAME_SLOT_SIZE;
	tail_size = in_len - tail_start;
	if (tail_size == 0)
		return 0;

	frame_table_bytes = (size_t)frames * SHPX_FRAME_SLOT_SIZE;
	clip_table_bytes = (size_t)frames * 8u;
	meta_size = SHPX_PREFIX_SIZE + frame_table_bytes + clip_table_bytes;

	if (pool->size & 1u) {
		if (!pool_append_byte(pool, 0))
			return 0;
	}
	pool_begin = (uint32_t)pool->size;
	pool_size = (uint32_t)tail_size;

	if (!pool_append(pool, in + tail_start, tail_size))
		return 0;
	if (tail_size & 1u) {
		if (!pool_append_byte(pool, 0))
			return 0;
	}

	out = (unsigned char *)malloc(meta_size);
	if (!out)
		return 0;

	memcpy(out, "SHPX", SHPX_MAGIC_SIZE);
	write_kf_header_be(out + SHPX_MAGIC_SIZE, in);
	write_be16(out + 0x12, pool->pool_id);
	write_be16(out + 0x14, 0);

	frame_off = SHPX_PREFIX_SIZE;
	clip_off = frame_off + frame_table_bytes;
	write_be32(out + 0x16, (uint32_t)frame_off);
	write_be32(out + 0x1A, (uint32_t)clip_off);
	write_be32(out + 0x1E, pool_begin);
	write_be32(out + 0x22, pool_size);

	if (opts && opts->verbose) {
		fprintf(stderr,
		    "shpx %08X: %u -> meta %zu tail %zu pool@%u size %u\n", (unsigned)opts->entry_crc,
		    (unsigned)frames, meta_size, tail_size, (unsigned)pool_begin, (unsigned)pool_size);
	}

	convert_frame_table(out + frame_off, in, frames, tail_start, in_len);
	if (!build_clips(in, in_len, frames, out + clip_off, opts)) {
		free(out);
		return 0;
	}

	*out_buf = out;
	*out_len = meta_size;
	return 1;
}

int remix_shpx_write_pool(const RemixShpxPool *pool, const char *out_mix_path)
{
	char pool_path[PATH_MAX];
	char pool_name[16];
	const char *slash;
	FILE *f;

	if (!pool || pool->size == 0 || !out_mix_path)
		return 1;
	if (pool->pool_id == 0)
		return 0;
	if (!remix_shpx_format_pool_name(pool->pool_id, pool_name, sizeof(pool_name)))
		return 0;

	slash = strrchr(out_mix_path, '/');
	if (!slash) {
		if (snprintf(pool_path, sizeof(pool_path), "%s", pool_name) >= (int)sizeof(pool_path))
			return 0;
	} else {
		size_t dir_len = (size_t)(slash - out_mix_path);
		if (dir_len + 1 + strlen(pool_name) + 1 > sizeof(pool_path))
			return 0;
		memcpy(pool_path, out_mix_path, dir_len);
		pool_path[dir_len] = '\0';
		if (snprintf(pool_path + dir_len, sizeof(pool_path) - dir_len, "/%s", pool_name)
		    >= (int)(sizeof(pool_path) - dir_len))
			return 0;
	}

	f = fopen(pool_path, "wb");
	if (!f)
		return 0;
	if (fwrite(pool->data, 1, pool->size, f) != pool->size) {
		fclose(f);
		return 0;
	}
	if (fclose(f) != 0)
		return 0;

	printf("wrote %s (%zu bytes)\n", pool_path, pool->size);
	return 1;
}
