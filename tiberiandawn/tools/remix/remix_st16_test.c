/*
 * Host smoke test for remix ST16 conversion (run: make test-st16).
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
	ICONS_STANDARD = 0x20u
};

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

int main(int argc, char **argv)
{
	const char *w16_dir = "../../atari-assets";
	unsigned char blob[ICONTROL_SIZE + ICON_COUNT * TILE_BYTES];
	size_t size;

	if (argc > 1) {
		w16_dir = argv[1];
	}

	size = build_standard_blob(blob, sizeof(blob));
	if (size == 0) {
		fprintf(stderr, "FAIL: could not build synthetic iconset\n");
		return 1;
	}
	{
		const size_t orig_size = size;

	if (!remix_st16_should_convert(blob, size)) {
		fprintf(stderr, "FAIL: synthetic blob not eligible for conversion\n");
		return 1;
	}

	if (!remix_st16_install_weights_from_file(w16_dir, "TEMPERAT")) {
		fprintf(stderr, "FAIL: could not load TEMPERAT.W16 from %s\n", w16_dir);
		return 1;
	}

	if (!remix_st16_convert_payload(blob, &size)) {
		fprintf(stderr, "FAIL: ST16_Convert_InPlace failed\n");
		return 1;
	}

	if (!remix_st16_is_native(blob, size)) {
		fprintf(stderr, "FAIL: output is not native ST16\n");
		return 1;
	}

	if (size >= orig_size) {
		fprintf(stderr, "FAIL: converted size %zu not smaller than original %zu\n", size, orig_size);
		return 1;
	}
	}

	printf("OK remix ST16 convert (%zu bytes, native ST16)\n", size);
	return 0;
}
