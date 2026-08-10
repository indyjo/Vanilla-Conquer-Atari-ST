/*
 * Host smoke / regression tests for remix ST16 conversion (run: make test-st16).
 *
 * Curated iconsets live in testdata/iconsets/ (extract with
 * testdata/extract_iconsets.py from an unmodified TEMPERAT.MIX).
 */

#include "remix_st16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	TILE_W = 24,
	TILE_H = 24,
	TILE_BYTES = TILE_W * TILE_H,
	ICON_COUNT = 2,
	ICONTROL_SIZE = 32u,
	ICONS_STANDARD = 0x20u,
	ICONS_V1 = 0x2cu,
	CHUNK_OFFSET = 0x20u,
	FLAG_HAS_MASK = 0x0001u,
	PLANAR_STRIDE_24 = 384u,
	ICON_STRIDE_MASKED_24 = 480u
};

static unsigned short read_le16(const unsigned char *p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static unsigned int read_le32(const unsigned char *p)
{
	return (unsigned int)p[0]
		| ((unsigned int)p[1] << 8)
		| ((unsigned int)p[2] << 16)
		| ((unsigned int)p[3] << 24);
}

static unsigned short read_be16(const unsigned char *p)
{
	return (unsigned short)(((unsigned short)p[0] << 8) | (unsigned short)p[1]);
}

static unsigned int read_be32(const unsigned char *p)
{
	return ((unsigned int)p[0] << 24)
		| ((unsigned int)p[1] << 16)
		| ((unsigned int)p[2] << 8)
		| (unsigned int)p[3];
}

static void write_le16(unsigned char *p, unsigned short v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void write_le32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
	p[2] = (unsigned char)((v >> 16) & 0xFFu);
	p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static size_t build_standard_blob(unsigned char *out, size_t cap)
{
	const size_t icons_bytes = (size_t)ICON_COUNT * (size_t)TILE_BYTES;
	const size_t total = (size_t)ICONTROL_SIZE + icons_bytes;
	size_t i;

	if (!out || cap < total) {
		return 0;
	}

	memset(out, 0, total);
	write_le16(out + 0, TILE_W);
	write_le16(out + 2, TILE_H);
	write_le16(out + 4, ICON_COUNT);
	write_le32(out + 8, (unsigned int)total);
	write_le32(out + 12, ICONS_STANDARD);

	for (i = ICONTROL_SIZE; i < total; ++i) {
		out[i] = (unsigned char)(1 + (i & 15));
	}
	return total;
}

static int read_file(const char *path, unsigned char **out_data, size_t *out_len)
{
	FILE *f;
	long sz;
	unsigned char *buf;

	*out_data = NULL;
	*out_len = 0;

	f = fopen(path, "rb");
	if (!f) {
		return 0;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return 0;
	}
	sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return 0;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return 0;
	}

	buf = (unsigned char *)malloc((size_t)sz);
	if (!buf) {
		fclose(f);
		return 0;
	}
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		return 0;
	}
	fclose(f);
	*out_data = buf;
	*out_len = (size_t)sz;
	return 1;
}

static int test_synthetic(const char *w16_dir)
{
	unsigned char blob[ICONTROL_SIZE + ICON_COUNT * TILE_BYTES];
	size_t size;
	size_t orig_size;

	size = build_standard_blob(blob, sizeof(blob));
	if (size == 0) {
		fprintf(stderr, "FAIL synthetic: could not build iconset\n");
		return 1;
	}
	orig_size = size;

	if (!remix_st16_should_convert(blob, size)) {
		fprintf(stderr, "FAIL synthetic: not eligible for conversion\n");
		return 1;
	}
	if (!remix_st16_install_weights_from_file(w16_dir, "TEMPERAT")) {
		fprintf(stderr, "FAIL synthetic: could not load TEMPERAT.W16 from %s\n", w16_dir);
		return 1;
	}
	if (!remix_st16_convert_payload(blob, &size)) {
		fprintf(stderr, "FAIL synthetic: ST16 convert failed\n");
		return 1;
	}
	if (!remix_st16_is_native(blob, size)) {
		fprintf(stderr, "FAIL synthetic: output is not native ST16\n");
		return 1;
	}
	if (size >= orig_size) {
		fprintf(stderr, "FAIL synthetic: converted size %zu not smaller than %zu\n", size, orig_size);
		return 1;
	}

	printf("OK synthetic ST16 convert (%zu bytes)\n", size);
	return 0;
}

/*
 * Retail CD P04.TEM (TEMPERAT.MIX on GDI/NOD): Count=103, 2 images, TransFlag=[1,0].
 * Image 0 is color-keyed with a single transparent pixel at (4,10); image 1 is opaque.
 * Convert must keep Count/images/HAS_MASK, and the 1bpp mask must match that keying
 * (visible x0..23 opaque except (4,10); planar pad x24..31 transparent) — not the
 * LE host bug that byte-swaps mask words into a fully transparent right half.
 */
static int mask_preserve_bit(const unsigned char *mask, int x, int y)
{
	unsigned short word;
	int bit;

	word = read_be16(mask + (size_t)y * 4u + (size_t)(x >> 4) * 2u);
	bit = 15 - (x & 15);
	return (word >> bit) & 1u;
}

static int test_p04_tem(const char *w16_dir, const char *iconsets_dir)
{
	char path[512];
	unsigned char *blob = NULL;
	size_t size = 0;
	size_t orig_size;
	unsigned short count_in;
	unsigned int trans_in;
	unsigned int icons_in;
	unsigned int map_in;
	unsigned short count_out;
	unsigned short flags_out;
	unsigned int icons_out;
	unsigned int map_out;
	unsigned int trans_out;
	unsigned int icon_stride;
	unsigned int nimg;
	unsigned int nimg_in;
	const unsigned char *mask0;
	int x;
	int y;
	int right_half_transparent = 0;
	int pad_opaque = 0;
	int failures = 0;

	snprintf(path, sizeof(path), "%s/P04.TEM", iconsets_dir);
	if (!read_file(path, &blob, &size)) {
		fprintf(stderr, "FAIL P04.TEM: cannot read %s\n", path);
		return 1;
	}
	orig_size = size;

	if (size < ICONTROL_SIZE) {
		fprintf(stderr, "FAIL P04.TEM: truncated (%zu bytes)\n", size);
		free(blob);
		return 1;
	}

	count_in = read_le16(blob + 4);
	icons_in = read_le32(blob + 12);
	trans_in = read_le32(blob + 24);
	map_in = read_le32(blob + 28);

	if (count_in != 103 || size != 1289) {
		fprintf(stderr,
			"FAIL P04.TEM: unexpected retail CD fixture (Count=%u size=%zu; want 103 / 1289)\n",
			count_in,
			size);
		failures++;
	}
	if (icons_in != ICONS_STANDARD) {
		fprintf(stderr, "FAIL P04.TEM: unexpected input Icons=%u (want 0x20)\n", icons_in);
		failures++;
	}
	if (map_in <= icons_in || ((map_in - icons_in) % TILE_BYTES) != 0) {
		fprintf(stderr, "FAIL P04.TEM: cannot derive input image count\n");
		free(blob);
		return 1;
	}
	nimg_in = (map_in - icons_in) / TILE_BYTES;
	if (nimg_in != 2) {
		fprintf(stderr, "FAIL P04.TEM: unexpected input images %u (want 2)\n", nimg_in);
		failures++;
	}
	if (trans_in == 0 || trans_in + 1 >= size || blob[trans_in] != 1 || blob[trans_in + 1] != 0) {
		fprintf(stderr, "FAIL P04.TEM: input TransFlag must be [1,0]\n");
		failures++;
	}
	if (!remix_st16_should_convert(blob, size)) {
		fprintf(stderr, "FAIL P04.TEM: not eligible for conversion\n");
		free(blob);
		return 1;
	}
	if (!remix_st16_install_weights_from_file(w16_dir, "TEMPERAT")) {
		fprintf(stderr, "FAIL P04.TEM: could not load TEMPERAT.W16 from %s\n", w16_dir);
		free(blob);
		return 1;
	}
	if (!remix_st16_convert_payload(blob, &size)) {
		fprintf(stderr, "FAIL P04.TEM: ST16 convert failed\n");
		free(blob);
		return 1;
	}
	if (!remix_st16_is_native(blob, size)) {
		fprintf(stderr, "FAIL P04.TEM: output is not native ST16\n");
		free(blob);
		return 1;
	}

	/* Offline convert leaves numeric fields big-endian. */
	count_out = read_be16(blob + 4);
	icons_out = read_be32(blob + 12);
	trans_out = read_be32(blob + 24);
	map_out = read_be32(blob + 28);
	flags_out = read_be16(blob + CHUNK_OFFSET + 8);

	if (count_out != 103) {
		fprintf(stderr, "FAIL P04.TEM: Count %u -> %u (must stay 103)\n", count_in, count_out);
		failures++;
	}
	if (!(flags_out & FLAG_HAS_MASK)) {
		fprintf(stderr, "FAIL P04.TEM: missing HAS_MASK (TransFlag[0] was 1)\n");
		failures++;
	}
	if (icons_out != ICONS_V1) {
		fprintf(stderr, "FAIL P04.TEM: Icons offset %u (want 0x2c)\n", icons_out);
		failures++;
	}
	if (size != 1109) {
		fprintf(stderr, "FAIL P04.TEM: output size %zu (want 1109)\n", size);
		failures++;
	}

	icon_stride = (flags_out & FLAG_HAS_MASK) ? ICON_STRIDE_MASKED_24 : PLANAR_STRIDE_24;
	if (map_out <= icons_out || ((map_out - icons_out) % icon_stride) != 0) {
		fprintf(stderr,
			"FAIL P04.TEM: cannot derive image count (icons=%u map=%u stride=%u)\n",
			icons_out,
			map_out,
			icon_stride);
		failures++;
		nimg = 0;
	} else {
		nimg = (map_out - icons_out) / icon_stride;
		if (nimg != 2) {
			fprintf(stderr, "FAIL P04.TEM: physical images %u (want 2)\n", nimg);
			failures++;
		}
	}

	if (trans_out + 1 >= size || blob[trans_out] != 1 || blob[trans_out + 1] != 0) {
		fprintf(stderr, "FAIL P04.TEM: output TransFlag must stay [1,0]\n");
		failures++;
	}
	if (map_out >= size || blob[map_out] != 0) {
		fprintf(stderr, "FAIL P04.TEM: output Map[0]=%u (want 0)\n",
			map_out < size ? blob[map_out] : 0xffu);
		failures++;
	}
	if (size >= orig_size) {
		fprintf(stderr, "FAIL P04.TEM: size did not shrink (%zu -> %zu)\n", orig_size, size);
		failures++;
	}

	/* Mask geometry for image 0 (BE bit order, 32px planar width). */
	if ((flags_out & FLAG_HAS_MASK) && nimg >= 1 && icons_out + ICON_STRIDE_MASKED_24 <= size) {
		mask0 = blob + icons_out + PLANAR_STRIDE_24;
		for (y = 0; y < TILE_H; ++y) {
			for (x = 16; x < TILE_W; ++x) {
				if (mask_preserve_bit(mask0, x, y)) {
					right_half_transparent++;
				}
			}
			for (x = TILE_W; x < 32; ++x) {
				if (!mask_preserve_bit(mask0, x, y)) {
					pad_opaque++;
				}
			}
		}
		if (right_half_transparent != 0) {
			fprintf(stderr,
				"FAIL P04.TEM: mask marks %d pixels in visible x16..23 transparent "
				"(CD has none there; likely LE mask-word byte swap)\n",
				right_half_transparent);
			failures++;
		}
		if (pad_opaque != 0) {
			fprintf(stderr,
				"FAIL P04.TEM: mask leaves %d planar-pad pixels (x24..31) opaque\n",
				pad_opaque);
			failures++;
		}
		if (!mask_preserve_bit(mask0, 4, 10)) {
			fprintf(stderr, "FAIL P04.TEM: mask missing CD key pixel (4,10)\n");
			failures++;
		}
	}

	free(blob);
	if (failures) {
		fprintf(stderr, "FAIL P04.TEM: %d check(s) failed after ST16 convert\n", failures);
		return failures;
	}
	printf("OK P04.TEM ST16 convert (%zu -> %zu bytes, Count=103, masked)\n", orig_size, size);
	return 0;
}

int main(int argc, char **argv)
{
	const char *w16_dir = "../../atari-assets";
	const char *iconsets_dir = "testdata/iconsets";
	int failures = 0;

	if (argc > 1) {
		w16_dir = argv[1];
	}
	if (argc > 2) {
		iconsets_dir = argv[2];
	}

	failures += test_synthetic(w16_dir);
	failures += test_p04_tem(w16_dir, iconsets_dir);

	if (failures) {
		fprintf(stderr, "remix ST16 tests: FAIL (%d)\n", failures);
		return 1;
	}
	printf("remix ST16 tests: PASS\n");
	return 0;
}
