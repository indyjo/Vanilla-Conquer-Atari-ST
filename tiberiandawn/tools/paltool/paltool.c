/*
 * paltool.c - Extract 768-byte VGA palettes from BMP, CPS, or WSA files.
 *
 * Output .PAL layout: 256 × RGB, channels 0..63 (6-bit DAC values).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define PAL_BYTES 768

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int path_has_ext(const char *path, const char *ext)
{
	size_t plen = strlen(path);
	size_t elen = strlen(ext);
	size_t i;

	if (plen < elen)
		return 0;
	for (i = 0; i < elen; i++) {
		char a = path[plen - elen + i];
		char b = ext[i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

static unsigned char bmp8_to_vga6(unsigned char v)
{
	return (unsigned char)((v * 63u + 127u) / 255u);
}

static int extract_bmp(FILE *f, const char *path, unsigned char *dst768)
{
	unsigned char file_hdr[14];
	unsigned char info_hdr[40];
	unsigned long off_bits;
	long width;
	long height;
	unsigned planes;
	unsigned bpp;
	unsigned long compression;
	unsigned n_colors;
	unsigned i;

	if (fread(file_hdr, 1, sizeof(file_hdr), f) != sizeof(file_hdr)) {
		fprintf(stderr, "error: %s: short read (BMP file header)\n", path);
		return 0;
	}
	if (file_hdr[0] != 'B' || file_hdr[1] != 'M') {
		fprintf(stderr, "error: %s: not a BMP (bad signature)\n", path);
		return 0;
	}
	off_bits = read_le32(file_hdr + 10);

	if (fread(info_hdr, 1, sizeof(info_hdr), f) != sizeof(info_hdr)) {
		fprintf(stderr, "error: %s: short read (BMP info header)\n", path);
		return 0;
	}
	if (read_le32(info_hdr) != 40) {
		fprintf(stderr, "error: %s: unsupported DIB header size\n", path);
		return 0;
	}
	width = (long)read_le32(info_hdr + 4);
	height = (long)read_le32(info_hdr + 8);
	planes = read_le16(info_hdr + 12);
	bpp = read_le16(info_hdr + 14);
	compression = read_le32(info_hdr + 16);

	if (width <= 0 || height == 0 || height == (long)0x80000000L) {
		fprintf(stderr, "error: %s: invalid BMP dimensions\n", path);
		return 0;
	}
	if (planes != 1 || bpp != 8) {
		fprintf(stderr, "error: %s: %u bpp not supported (8-bit indexed only)\n", path, bpp);
		return 0;
	}
	if (compression != 0 && compression != 1) {
		fprintf(stderr, "error: %s: compression %lu not supported (BI_RGB or BI_RLE8 only)\n", path,
			compression);
		return 0;
	}

	n_colors = read_le32(info_hdr + 32);
	if (n_colors == 0 || n_colors > 256)
		n_colors = 256;

	memset(dst768, 0, PAL_BYTES);
	for (i = 0; i < n_colors; i++) {
		unsigned char quad[4];
		if (fread(quad, 1, 4, f) != 4) {
			fprintf(stderr, "error: %s: short read in BMP color table\n", path);
			return 0;
		}
		dst768[i * 3 + 0] = bmp8_to_vga6(quad[2]); /* R */
		dst768[i * 3 + 1] = bmp8_to_vga6(quad[1]); /* G */
		dst768[i * 3 + 2] = bmp8_to_vga6(quad[0]); /* B */
	}

	(void)off_bits;
	return 1;
}

static int extract_wsa(FILE *f, const char *path, long size, unsigned char *dst768)
{
	unsigned char hdr[14];
	uint16_t total_frames;
	uint16_t flags;
	long pal_off;

	if (size < 14 + PAL_BYTES) {
		fprintf(stderr, "error: %s: file too short for WSA\n", path);
		return 0;
	}
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fprintf(stderr, "error: %s: short read (WSA header)\n", path);
		return 0;
	}
	total_frames = read_le16(hdr + 0);
	flags = read_le16(hdr + 12);
	if ((flags & 1u) == 0u) {
		fprintf(stderr, "error: %s: WSA has no embedded palette\n", path);
		return 0;
	}
	pal_off = 14L + (long)(total_frames + 2) * 4L;
	if (pal_off < 0 || pal_off + PAL_BYTES > size) {
		fprintf(stderr, "error: %s: invalid palette offset in WSA\n", path);
		return 0;
	}
	if (fseek(f, pal_off, SEEK_SET) != 0) {
		fprintf(stderr, "error: %s: seek to WSA palette failed\n", path);
		return 0;
	}
	if (fread(dst768, 1, PAL_BYTES, f) != PAL_BYTES) {
		fprintf(stderr, "error: %s: short read (WSA palette)\n", path);
		return 0;
	}
	return 1;
}

static int extract_cps(FILE *f, const char *path, long size, unsigned char *dst768)
{
	unsigned char hdr[10];
	uint16_t skip;

	if (size < 10 + PAL_BYTES) {
		fprintf(stderr, "error: %s: file too short for CPS\n", path);
		return 0;
	}
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fprintf(stderr, "error: %s: short read (CPS header)\n", path);
		return 0;
	}
	skip = read_le16(hdr + 8);
	if (skip != PAL_BYTES) {
		fprintf(stderr, "error: %s: CPS has no embedded palette (skip=%u, expected %d)\n", path, skip,
			PAL_BYTES);
		return 0;
	}
	if (fread(dst768, 1, PAL_BYTES, f) != PAL_BYTES) {
		fprintf(stderr, "error: %s: short read (CPS embedded palette)\n", path);
		return 0;
	}
	return 1;
}

static int extract_palette(const char *path, unsigned char *dst768)
{
	FILE *f;
	long size;
	int ok = 0;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	if (fseek(f, 0L, SEEK_END) != 0) {
		fprintf(stderr, "error: failed to seek in %s\n", path);
		fclose(f);
		return 0;
	}
	size = ftell(f);
	if (size < 0) {
		fprintf(stderr, "error: failed to query size of %s\n", path);
		fclose(f);
		return 0;
	}
	if (fseek(f, 0L, SEEK_SET) != 0) {
		fprintf(stderr, "error: failed to rewind %s\n", path);
		fclose(f);
		return 0;
	}

	if (path_has_ext(path, ".pal")) {
		if (size != PAL_BYTES) {
			fprintf(stderr, "error: %s: expected %d-byte .PAL, got %ld\n", path, PAL_BYTES, size);
			fclose(f);
			return 0;
		}
		ok = (fread(dst768, 1, PAL_BYTES, f) == PAL_BYTES);
		if (!ok)
			fprintf(stderr, "error: %s: short read\n", path);
	} else if (path_has_ext(path, ".bmp")) {
		ok = extract_bmp(f, path, dst768);
	} else if (path_has_ext(path, ".wsa")) {
		ok = extract_wsa(f, path, size, dst768);
	} else if (path_has_ext(path, ".cps")) {
		ok = extract_cps(f, path, size, dst768);
	} else {
		fprintf(stderr, "error: %s: unknown input type (expected .bmp, .cps, .wsa, or .pal)\n", path);
	}

	fclose(f);
	return ok;
}

static int write_palette(const char *path, const unsigned char *pal768)
{
	FILE *f;

	f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "error: cannot write %s\n", path);
		return 0;
	}
	if (fwrite(pal768, 1, PAL_BYTES, f) != PAL_BYTES) {
		fprintf(stderr, "error: short write to %s\n", path);
		fclose(f);
		return 0;
	}
	fclose(f);
	return 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"paltool - extract 768-byte VGA palettes from BMP, CPS, or WSA\n"
		"\n"
		"Usage:\n"
		"  %s -i INFILE -o OUTFILE.PAL\n"
		"\n"
		"Inputs:\n"
		"  .bmp   8-bit indexed BMP (BI_RGB or BI_RLE8); RGB converted to 6-bit\n"
		"  .cps   Westwood CPS with embedded 768-byte skip palette (TITLE, ATTRACT2, …)\n"
		"  .wsa   WSA with flags bit0 set (palette after frame offset table)\n"
		"  .pal   raw 768-byte palette (copied verbatim)\n"
		"\n"
		"Output:\n"
		"  768-byte .PAL: 256 × RGB, channels 0..63 (VGA 6-bit DAC values)\n"
		"\n"
		"Options:\n"
		"  -h, --help   Show this help\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *in_path = NULL;
	const char *out_path = NULL;
	unsigned char pal768[PAL_BYTES];
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "-i") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: -i requires an argument\n");
				return 1;
			}
			in_path = argv[++i];
			continue;
		}
		if (strncmp(argv[i], "-i", 2) == 0 && argv[i][2] != '\0') {
			in_path = argv[i] + 2;
			continue;
		}
		if (strcmp(argv[i], "-o") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: -o requires an argument\n");
				return 1;
			}
			out_path = argv[++i];
			continue;
		}
		if (strncmp(argv[i], "-o", 2) == 0 && argv[i][2] != '\0') {
			out_path = argv[i] + 2;
			continue;
		}
		fprintf(stderr, "error: unknown option: %s\n", argv[i]);
		usage(argv[0]);
		return 1;
	}

	if (!in_path || !out_path) {
		fprintf(stderr, "error: both -i INFILE and -o OUTFILE.PAL are required\n");
		usage(argv[0]);
		return 1;
	}

	if (!extract_palette(in_path, pal768))
		return 1;
	if (!write_palette(out_path, pal768))
		return 1;

	fprintf(stderr, "wrote %s (%d bytes) from %s\n", out_path, PAL_BYTES, in_path);
	return 0;
}
