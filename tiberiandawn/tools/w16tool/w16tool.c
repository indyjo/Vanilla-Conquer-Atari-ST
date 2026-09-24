/*
 * w16tool.c - Inspect C2P .W16 bundles, dither an 8-bit BMP onto the 16 pens,
 * and draw a 16x16 palette matrix with subset swatches framed.
 *
 * .W16 layout: magic "W16\0", 16 subset indices, then weights[256][16].
 * Each weight row sums to 16. Bayer mapping matches c2p.cpp
 * C2P_Bayer4_Color_Lorez (4x4 ranks 0..15).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define W16_MAGIC "W16"
#define W16_FILE_BYTES (4 + 16 + 256 * 16)
#define PAL_BYTES 768

#define BI_RGB 0
#define BI_RLE8 1

#define MATRIX_CELLS 16
#define SWATCH 8
#define GAP 4
#define MARGIN 2
#define MATRIX_SIZE (MARGIN + MATRIX_CELLS * SWATCH + (MATRIX_CELLS - 1) * GAP + MARGIN)

typedef struct {
	unsigned char subset[16];
	unsigned char weights[256][16];
	long file_bytes;
} W16Data;

typedef struct {
	long width;
	long height;
	unsigned char palette[256][4]; /* B, G, R, 0 */
	unsigned char *pixels; /* row 0 is the top of the image */
} Bmp8;

/* 4x4 Bayer threshold matrix, values 0..15. Same order as c2p.cpp. */
static const unsigned char Bayer4x4[16] = {
	0,  8,  2, 10,
	12, 4, 14, 6,
	3, 11, 1,  9,
	15, 7, 13, 5
};

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static int write_le16(FILE *f, uint16_t v)
{
	unsigned char b[2];
	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
	return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int write_le32(FILE *f, uint32_t v)
{
	unsigned char b[4];
	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
	b[2] = (unsigned char)(v >> 16);
	b[3] = (unsigned char)(v >> 24);
	return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static int subset_has_dup(const unsigned char *s, int n)
{
	int i, j;
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (s[i] == s[j])
				return 1;
		}
	}
	return 0;
}

static int igcd(int a, int b)
{
	while (b) {
		int t = a % b;
		a = b;
		b = t;
	}
	return a < 0 ? -a : a;
}

static int load_w16(const char *path, W16Data *out)
{
	unsigned char hdr[4 + 16];
	FILE *f;
	long size;
	int src;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fprintf(stderr, "error: cannot seek %s\n", path);
		fclose(f);
		return 0;
	}
	size = ftell(f);
	if (size < 0) {
		fprintf(stderr, "error: cannot tell size of %s\n", path);
		fclose(f);
		return 0;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fprintf(stderr, "error: cannot seek %s\n", path);
		fclose(f);
		return 0;
	}
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fprintf(stderr, "error: short read in %s\n", path);
		fclose(f);
		return 0;
	}
	if (memcmp(hdr, W16_MAGIC, 4) != 0) {
		fprintf(stderr, "error: %s: bad magic (expected W16)\n", path);
		fclose(f);
		return 0;
	}
	memcpy(out->subset, hdr + 4, 16);
	for (src = 0; src < 256; src++) {
		if (fread(out->weights[src], 1, 16, f) != 16) {
			fprintf(stderr, "error: short read weights row %d in %s\n", src, path);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	out->file_bytes = size;

	if (subset_has_dup(out->subset, 16)) {
		fprintf(stderr, "error: subset in %s contains duplicate indices\n", path);
		return 0;
	}
	for (src = 0; src < 256; src++) {
		int sum = 0;
		int k;
		for (k = 0; k < 16; k++)
			sum += (int)out->weights[src][k];
		if (sum != 16) {
			fprintf(stderr, "error: weights row %d sums to %d (expected 16)\n", src, sum);
			return 0;
		}
	}
	return 1;
}

/* STDOOM / c2p.cpp C2P_Bayer4_Color_Lorez. y is from the top of the image. */
static int bayer_pen(const unsigned char *weights, int x, int y)
{
	unsigned char bayer_lwb = 0;
	unsigned char bayer_upb = 0;
	int rank = (int)Bayer4x4[((y & 3) << 2) | (x & 3)];
	int c;

	for (c = 0; c < 16; c++) {
		bayer_upb = (unsigned char)(bayer_upb + weights[c]);
		if (rank >= (int)bayer_lwb && rank < (int)bayer_upb)
			return c;
		bayer_lwb = (unsigned char)(bayer_lwb + weights[c]);
	}
	return 15;
}

static const char *granularity_note(int g)
{
	if (g == 1)
		return " (4x4 Bayer)";
	if (g == 4)
		return " (2x2 Bayer)";
	return "";
}

static int cmd_info(const char *path)
{
	W16Data data;
	int g = 0;
	int min_nz = 16;
	int max_nz = 0;
	int src;
	int i;

	if (!load_w16(path, &data))
		return 1;

	for (src = 0; src < 256; src++) {
		int nz = 0;
		int k;
		for (k = 0; k < 16; k++) {
			int w = (int)data.weights[src][k];
			if (w) {
				g = g ? igcd(g, w) : w;
				nz++;
			}
		}
		if (nz < min_nz)
			min_nz = nz;
		if (nz > max_nz)
			max_nz = nz;
	}

	printf("%s: %ld bytes\n", path, data.file_bytes);
	printf("subset:");
	for (i = 0; i < 16; i++)
		printf(" %u", (unsigned)data.subset[i]);
	printf("\n");
	printf("weights: 256 rows, each sums to 16\n");
	printf("granularity: %d%s\n", g, granularity_note(g));
	printf("nonzero pens: min %d, max %d\n", min_nz, max_nz);
	return 0;
}

static void bmp_free(Bmp8 *bmp)
{
	free(bmp->pixels);
	bmp->pixels = NULL;
}

static int bmp_read_u8(FILE *f, unsigned char *out)
{
	return fread(out, 1, 1, f) == 1;
}

static int bmp_decode_rle8(FILE *f, const char *path, Bmp8 *bmp)
{
	long x = 0;
	long y = 0;
	int done = 0;

	while (!done) {
		unsigned char b0;
		unsigned char b1;

		if (!bmp_read_u8(f, &b0)) {
			if (feof(f))
				break;
			fprintf(stderr, "error: %s: short read in RLE8 data\n", path);
			return 0;
		}
		if (b0 != 0) {
			long dest_y;
			if (!bmp_read_u8(f, &b1)) {
				fprintf(stderr, "error: %s: short read in RLE8 run\n", path);
				return 0;
			}
			dest_y = bmp->height - 1 - y;
			while (b0-- > 0) {
				if (x >= 0 && x < bmp->width && dest_y >= 0 && dest_y < bmp->height)
					bmp->pixels[dest_y * bmp->width + x] = b1;
				x++;
			}
			continue;
		}
		if (!bmp_read_u8(f, &b1)) {
			fprintf(stderr, "error: %s: short read in RLE8 escape\n", path);
			return 0;
		}
		if (b1 == 0) {
			x = 0;
			y++;
			if (y >= bmp->height)
				done = 1;
		} else if (b1 == 1) {
			done = 1;
		} else if (b1 == 2) {
			unsigned char dx;
			unsigned char dy;
			if (!bmp_read_u8(f, &dx) || !bmp_read_u8(f, &dy)) {
				fprintf(stderr, "error: %s: short read in RLE8 delta\n", path);
				return 0;
			}
			x += (long)dx;
			y += (long)dy;
		} else {
			unsigned run = b1;
			long dest_y = bmp->height - 1 - y;
			while (run-- > 0) {
				unsigned char idx;
				if (!bmp_read_u8(f, &idx)) {
					fprintf(stderr, "error: %s: short read in RLE8 absolute run\n", path);
					return 0;
				}
				if (x >= 0 && x < bmp->width && dest_y >= 0 && dest_y < bmp->height)
					bmp->pixels[dest_y * bmp->width + x] = idx;
				x++;
			}
			if (b1 & 1) {
				unsigned char pad;
				if (!bmp_read_u8(f, &pad)) {
					fprintf(stderr, "error: %s: short read in RLE8 pad byte\n", path);
					return 0;
				}
			}
		}
	}
	return 1;
}

static int load_bmp8(const char *path, Bmp8 *bmp)
{
	unsigned char file_hdr[14];
	unsigned char info_hdr[40];
	FILE *f;
	unsigned long off_bits;
	long height_raw;
	int top_down;
	unsigned planes;
	unsigned bpp;
	unsigned long compression;
	unsigned n_colors;
	unsigned i;
	size_t npx;

	memset(bmp, 0, sizeof(*bmp));
	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	if (fread(file_hdr, 1, sizeof(file_hdr), f) != sizeof(file_hdr)) {
		fprintf(stderr, "error: %s: short read (BMP file header)\n", path);
		fclose(f);
		return 0;
	}
	if (file_hdr[0] != 'B' || file_hdr[1] != 'M') {
		fprintf(stderr, "error: %s: not a BMP (bad signature)\n", path);
		fclose(f);
		return 0;
	}
	off_bits = read_le32(file_hdr + 10);
	if (fread(info_hdr, 1, sizeof(info_hdr), f) != sizeof(info_hdr)) {
		fprintf(stderr, "error: %s: short read (BMP info header)\n", path);
		fclose(f);
		return 0;
	}
	if (read_le32(info_hdr) != 40) {
		fprintf(stderr, "error: %s: unsupported DIB header size\n", path);
		fclose(f);
		return 0;
	}
	bmp->width = (long)read_le32(info_hdr + 4);
	height_raw = (long)read_le32(info_hdr + 8);
	planes = read_le16(info_hdr + 12);
	bpp = read_le16(info_hdr + 14);
	compression = read_le32(info_hdr + 16);

	if (bmp->width <= 0 || height_raw == 0 || height_raw == (long)0x80000000L) {
		fprintf(stderr, "error: %s: invalid BMP dimensions\n", path);
		fclose(f);
		return 0;
	}
	top_down = height_raw < 0;
	bmp->height = top_down ? -height_raw : height_raw;
	if (planes != 1 || bpp != 8) {
		fprintf(stderr, "error: %s: %u bpp not supported (8-bit indexed only)\n", path, bpp);
		fclose(f);
		return 0;
	}
	if (compression != BI_RGB && compression != BI_RLE8) {
		fprintf(stderr, "error: %s: compression %lu not supported (BI_RGB or BI_RLE8 only)\n", path,
			compression);
		fclose(f);
		return 0;
	}
	if (compression == BI_RLE8 && top_down) {
		fprintf(stderr, "error: %s: top-down RLE8 is not supported\n", path);
		fclose(f);
		return 0;
	}

	n_colors = read_le32(info_hdr + 32);
	if (n_colors == 0 || n_colors > 256)
		n_colors = 256;

	memset(bmp->palette, 0, sizeof(bmp->palette));
	for (i = 0; i < n_colors; i++) {
		if (fread(bmp->palette[i], 1, 4, f) != 4) {
			fprintf(stderr, "error: %s: short read in BMP color table\n", path);
			fclose(f);
			return 0;
		}
	}

	if ((unsigned long)bmp->width > (unsigned long)-1 / (unsigned long)bmp->height) {
		fprintf(stderr, "error: %s: image too large\n", path);
		fclose(f);
		return 0;
	}
	npx = (size_t)bmp->width * (size_t)bmp->height;
	bmp->pixels = (unsigned char *)malloc(npx);
	if (!bmp->pixels) {
		fprintf(stderr, "error: out of memory\n");
		fclose(f);
		return 0;
	}
	memset(bmp->pixels, 0, npx);

	if (fseek(f, (long)off_bits, SEEK_SET) != 0) {
		fprintf(stderr, "error: %s: seek to pixel data failed\n", path);
		fclose(f);
		bmp_free(bmp);
		return 0;
	}

	if (compression == BI_RLE8) {
		if (!bmp_decode_rle8(f, path, bmp)) {
			fclose(f);
			bmp_free(bmp);
			return 0;
		}
	} else {
		unsigned row_bytes = (unsigned)((bmp->width + 3) & ~3);
		unsigned char *row = (unsigned char *)malloc(row_bytes);
		long file_y;
		if (!row) {
			fprintf(stderr, "error: out of memory\n");
			fclose(f);
			bmp_free(bmp);
			return 0;
		}
		for (file_y = 0; file_y < bmp->height; file_y++) {
			long dest_y = top_down ? file_y : (bmp->height - 1 - file_y);
			if (fread(row, 1, row_bytes, f) != row_bytes) {
				fprintf(stderr, "error: %s: short read at row %ld\n", path, file_y);
				free(row);
				fclose(f);
				bmp_free(bmp);
				return 0;
			}
			memcpy(bmp->pixels + (size_t)dest_y * (size_t)bmp->width, row, (size_t)bmp->width);
		}
		free(row);
	}

	fclose(f);
	return 1;
}

static int write_bmp8(const char *path, long width, long height, const unsigned char *pixels,
	const unsigned char palette[256][4])
{
	FILE *fp;
	uint32_t stride;
	uint32_t img_size;
	uint32_t off_bits;
	uint32_t file_size;
	long row;
	unsigned i;

	if (width <= 0 || height <= 0) {
		fprintf(stderr, "error: invalid BMP dimensions\n");
		return 0;
	}
	stride = ((uint32_t)width + 3u) & ~3u;
	img_size = stride * (uint32_t)height;
	off_bits = 14u + 40u + 256u * 4u;
	file_size = off_bits + img_size;

	fp = fopen(path, "wb");
	if (!fp) {
		fprintf(stderr, "error: cannot write %s\n", path);
		return 0;
	}
	if (fputc('B', fp) == EOF || fputc('M', fp) == EOF || write_le32(fp, file_size) != 0 ||
	    write_le16(fp, 0) != 0 || write_le16(fp, 0) != 0 || write_le32(fp, off_bits) != 0) {
		fprintf(stderr, "error: cannot write BMP header to %s\n", path);
		fclose(fp);
		return 0;
	}
	if (write_le32(fp, 40) != 0 || write_le32(fp, (uint32_t)width) != 0 ||
	    write_le32(fp, (uint32_t)height) != 0 || write_le16(fp, 1) != 0 || write_le16(fp, 8) != 0 ||
	    write_le32(fp, 0) != 0 || write_le32(fp, img_size) != 0 || write_le32(fp, 0) != 0 ||
	    write_le32(fp, 0) != 0 || write_le32(fp, 256) != 0 || write_le32(fp, 256) != 0) {
		fprintf(stderr, "error: cannot write BMP info to %s\n", path);
		fclose(fp);
		return 0;
	}
	for (i = 0; i < 256; i++) {
		if (fwrite(palette[i], 1, 4, fp) != 4) {
			fprintf(stderr, "error: cannot write BMP palette to %s\n", path);
			fclose(fp);
			return 0;
		}
	}
	for (row = height - 1; row >= 0; row--) {
		unsigned char pad[3] = {0, 0, 0};
		unsigned pad_n = (unsigned)(stride - (uint32_t)width);
		const unsigned char *src = pixels + (size_t)row * (size_t)width;
		if (fwrite(src, 1, (size_t)width, fp) != (size_t)width ||
		    (pad_n && fwrite(pad, 1, pad_n, fp) != pad_n)) {
			fprintf(stderr, "error: cannot write BMP pixels to %s\n", path);
			fclose(fp);
			return 0;
		}
	}
	fclose(fp);
	return 1;
}

static int cmd_apply(int argc, char **argv)
{
	const char *w16_path = NULL;
	const char *in_path = NULL;
	const char *out_path = NULL;
	W16Data w16;
	Bmp8 src;
	unsigned char (*out_pal)[4];
	unsigned char *out_px;
	long y;
	int i;
	int k;
	size_t npx;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: -o needs a path\n");
				return 1;
			}
			out_path = argv[++i];
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", argv[i]);
			return 1;
		} else if (!w16_path) {
			w16_path = argv[i];
		} else if (!in_path) {
			in_path = argv[i];
		} else {
			fprintf(stderr, "error: unexpected argument %s\n", argv[i]);
			return 1;
		}
	}
	if (!w16_path || !in_path || !out_path) {
		fprintf(stderr, "usage: w16tool apply FILE.w16 IN.bmp -o OUT.bmp\n");
		return 1;
	}
	if (!load_w16(w16_path, &w16))
		return 1;
	if (!load_bmp8(in_path, &src))
		return 1;

	npx = (size_t)src.width * (size_t)src.height;
	out_px = (unsigned char *)malloc(npx);
	out_pal = calloc(256, 4);
	if (!out_px || !out_pal) {
		fprintf(stderr, "error: out of memory\n");
		free(out_px);
		free(out_pal);
		bmp_free(&src);
		return 1;
	}
	for (k = 0; k < 16; k++)
		memcpy(out_pal[k], src.palette[w16.subset[k]], 4);

	for (y = 0; y < src.height; y++) {
		long x;
		for (x = 0; x < src.width; x++) {
			unsigned char idx = src.pixels[(size_t)y * (size_t)src.width + (size_t)x];
			out_px[(size_t)y * (size_t)src.width + (size_t)x] =
			    (unsigned char)bayer_pen(w16.weights[idx], (int)x, (int)y);
		}
	}

	if (!write_bmp8(out_path, src.width, src.height, out_px, (const unsigned char (*)[4])out_pal)) {
		free(out_px);
		free(out_pal);
		bmp_free(&src);
		return 1;
	}
	free(out_px);
	free(out_pal);
	bmp_free(&src);
	printf("wrote %s (%ld x %ld)\n", out_path, src.width, src.height);
	return 0;
}

static unsigned char vga6_to_8(unsigned char c)
{
	c = (unsigned char)(c & 63u);
	return (unsigned char)((c << 2) | (c >> 4));
}

static int color_dist2(const unsigned char *pal, int i, int r, int g, int b)
{
	int dr = (int)pal[i * 3 + 0] - r;
	int dg = (int)pal[i * 3 + 1] - g;
	int db = (int)pal[i * 3 + 2] - b;
	return dr * dr + dg * dg + db * db;
}

static int nearest_except(const unsigned char *pal, int r, int g, int b, int exclude)
{
	int best = -1;
	int best_d = 0;
	int i;

	for (i = 0; i < 256; i++) {
		int d;
		if (i == exclude)
			continue;
		d = color_dist2(pal, i, r, g, b);
		if (best < 0 || d < best_d) {
			best = i;
			best_d = d;
		}
	}
	return best;
}

static void draw_frame(unsigned char *px, int x0, int y0, int side, unsigned char color)
{
	int i;
	int x1 = x0 + side - 1;
	int y1 = y0 + side - 1;

	for (i = 0; i < side; i++) {
		px[(size_t)y0 * MATRIX_SIZE + (size_t)(x0 + i)] = color;
		px[(size_t)y1 * MATRIX_SIZE + (size_t)(x0 + i)] = color;
		px[(size_t)(y0 + i) * MATRIX_SIZE + (size_t)x0] = color;
		px[(size_t)(y0 + i) * MATRIX_SIZE + (size_t)x1] = color;
	}
}

static int load_pal(const char *path, unsigned char pal[PAL_BYTES])
{
	FILE *f;
	long pal_size;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fprintf(stderr, "error: cannot seek %s\n", path);
		fclose(f);
		return 0;
	}
	pal_size = ftell(f);
	if (pal_size < 0) {
		fprintf(stderr, "error: cannot tell size of %s\n", path);
		fclose(f);
		return 0;
	}
	if (pal_size != PAL_BYTES) {
		fprintf(stderr, "error: %s: expected %d-byte .PAL, got %ld bytes\n", path, PAL_BYTES, pal_size);
		if (pal_size >= 4) {
			unsigned char magic[4];
			if (fseek(f, 0, SEEK_SET) == 0 && fread(magic, 1, 4, f) == 4 &&
			    memcmp(magic, W16_MAGIC, 4) == 0)
				fprintf(stderr, "error: %s is a .W16 weight bundle, not a palette\n", path);
		}
		fclose(f);
		return 0;
	}
	if (fseek(f, 0, SEEK_SET) != 0 || fread(pal, 1, PAL_BYTES, f) != PAL_BYTES) {
		fprintf(stderr, "error: %s: short read\n", path);
		fclose(f);
		return 0;
	}
	fclose(f);
	return 1;
}

/* Lowest pen index wins a tie, matching C2P_MapNearestLUT. */
static int dominant_pen(const unsigned char *weights)
{
	int best_k = 0;
	int best_w = (int)weights[0];
	int k;

	for (k = 1; k < 16; k++) {
		if ((int)weights[k] > best_w) {
			best_w = (int)weights[k];
			best_k = k;
		}
	}
	return best_k;
}

static int cmd_matrix(int argc, char **argv)
{
	const char *w16_path = NULL;
	const char *pal_path = NULL;
	const char *out_path = NULL;
	W16Data w16;
	unsigned char pal[PAL_BYTES];
	unsigned char (*out_pal)[4];
	unsigned char *px;
	unsigned char in_subset[256];
	int black;
	int white;
	int i;
	int row;
	int col;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: -o needs a path\n");
				return 1;
			}
			out_path = argv[++i];
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", argv[i]);
			return 1;
		} else if (!w16_path) {
			w16_path = argv[i];
		} else if (!pal_path) {
			pal_path = argv[i];
		} else {
			fprintf(stderr, "error: unexpected argument %s\n", argv[i]);
			return 1;
		}
	}
	if (!w16_path || !pal_path || !out_path) {
		fprintf(stderr, "usage: w16tool matrix FILE.w16 IN.pal -o OUT.bmp\n");
		return 1;
	}
	if (!load_w16(w16_path, &w16))
		return 1;
	if (!load_pal(pal_path, pal))
		return 1;

	out_pal = calloc(256, 4);
	px = (unsigned char *)malloc((size_t)MATRIX_SIZE * (size_t)MATRIX_SIZE);
	if (!out_pal || !px) {
		fprintf(stderr, "error: out of memory\n");
		free(out_pal);
		free(px);
		return 1;
	}
	for (i = 0; i < 256; i++) {
		out_pal[i][0] = vga6_to_8(pal[i * 3 + 2]);
		out_pal[i][1] = vga6_to_8(pal[i * 3 + 1]);
		out_pal[i][2] = vga6_to_8(pal[i * 3 + 0]);
		out_pal[i][3] = 0;
	}

	black = nearest_except(pal, 0, 0, 0, -1);
	white = nearest_except(pal, 63, 63, 63, -1);
	if (white == black)
		white = nearest_except(pal, 63, 63, 63, black);

	memset(in_subset, 0, sizeof(in_subset));
	for (i = 0; i < 16; i++)
		in_subset[w16.subset[i]] = 1;

	memset(px, (unsigned char)black, (size_t)MATRIX_SIZE * (size_t)MATRIX_SIZE);
	for (row = 0; row < MATRIX_CELLS; row++) {
		for (col = 0; col < MATRIX_CELLS; col++) {
			int idx = row * MATRIX_CELLS + col;
			int ox = MARGIN + col * (SWATCH + GAP);
			int oy = MARGIN + row * (SWATCH + GAP);
			int sy, sx;
			/* Top half is the source color. Bottom half is that index Bayer-dithered
			 * onto the 16 subset pens (pixel value is the palette index of the pen). */
			for (sy = 0; sy < SWATCH / 2; sy++) {
				for (sx = 0; sx < SWATCH; sx++)
					px[(size_t)(oy + sy) * MATRIX_SIZE + (size_t)(ox + sx)] = (unsigned char)idx;
			}
			for (sy = SWATCH / 2; sy < SWATCH; sy++) {
				for (sx = 0; sx < SWATCH; sx++) {
					int pen = bayer_pen(w16.weights[idx], sx, sy);
					px[(size_t)(oy + sy) * MATRIX_SIZE + (size_t)(ox + sx)] = w16.subset[pen];
				}
			}
			if (in_subset[idx]) {
				draw_frame(px, ox - 2, oy - 2, SWATCH + 4, (unsigned char)white);
				draw_frame(px, ox - 1, oy - 1, SWATCH + 2, (unsigned char)black);
			}
		}
	}

	if (!write_bmp8(out_path, MATRIX_SIZE, MATRIX_SIZE, px, (const unsigned char (*)[4])out_pal)) {
		free(out_pal);
		free(px);
		return 1;
	}
	free(out_pal);
	free(px);
	printf("wrote %s (%d x %d)\n", out_path, MATRIX_SIZE, MATRIX_SIZE);
	return 0;
}

static int cmd_hex(int argc, char **argv)
{
	const char *w16_path = NULL;
	const char *pal_path = NULL;
	const char *out_path = NULL;
	W16Data w16;
	unsigned char pal[PAL_BYTES];
	unsigned char pen_of[256];
	FILE *f;
	int colors_only = 0;
	int i;
	int pen;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: -o needs a path\n");
				return 1;
			}
			out_path = argv[++i];
		} else if (!strcmp(argv[i], "--colors-only")) {
			colors_only = 1;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", argv[i]);
			return 1;
		} else if (!w16_path) {
			w16_path = argv[i];
		} else if (!pal_path) {
			pal_path = argv[i];
		} else {
			fprintf(stderr, "error: unexpected argument %s\n", argv[i]);
			return 1;
		}
	}
	if (!w16_path || !pal_path || !out_path) {
		fprintf(stderr, "usage: w16tool hex FILE.w16 IN.pal -o OUT.hex [--colors-only]\n");
		return 1;
	}
	if (!load_w16(w16_path, &w16))
		return 1;
	if (!load_pal(pal_path, pal))
		return 1;

	if (!colors_only) {
		for (i = 0; i < 256; i++)
			pen_of[i] = (unsigned char)dominant_pen(w16.weights[i]);
	}

	f = fopen(out_path, "wb");
	if (!f) {
		fprintf(stderr, "error: cannot write %s\n", out_path);
		return 1;
	}
	if (!colors_only) {
		fprintf(f, "# 16 hardware pens, one RRGGBB line each (Lospec / Aseprite .hex).\n");
		fprintf(f, "# \"= n,n\" lists palette indices whose dominant weight is that pen.\n");
		fprintf(f, "# atarist-reminiscence uses numbers 0-31 and ignores the rest.\n");
	}
	for (pen = 0; pen < 16; pen++) {
		int idx = (int)w16.subset[pen];
		unsigned char r = vga6_to_8(pal[idx * 3 + 0]);
		unsigned char g = vga6_to_8(pal[idx * 3 + 1]);
		unsigned char b = vga6_to_8(pal[idx * 3 + 2]);
		int first = 1;

		fprintf(f, "%02x%02x%02x", r, g, b);
		if (!colors_only) {
			for (i = 0; i < 256; i++) {
				if ((int)pen_of[i] != pen)
					continue;
				fprintf(f, first ? " = %d" : ",%d", i);
				first = 0;
			}
		}
		fputc('\n', f);
	}
	if (ferror(f)) {
		fprintf(stderr, "error: cannot write %s\n", out_path);
		fclose(f);
		return 1;
	}
	fclose(f);
	printf("wrote %s\n", out_path);
	return 0;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s <command> ...\n"
		"\n"
		"  info FILE.w16                  print subset and weight summary\n"
		"  apply FILE.w16 IN.bmp -o OUT.bmp\n"
		"                                 Bayer-dither to the 16 subset pens\n"
		"  matrix FILE.w16 IN.pal -o OUT.bmp\n"
		"                                 16x16 palette matrix; bottom half dithered\n"
		"  hex FILE.w16 IN.pal -o OUT.hex\n"
		"                                 16-pen Lospec .hex palette\n"
		"    --colors-only              RRGGBB lines only, no index lists\n",
		argv0);
}

int main(int argc, char **argv)
{
	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
		return argc < 2 ? 1 : 0;
	}
	if (!strcmp(argv[1], "info")) {
		if (argc != 3) {
			fprintf(stderr, "usage: %s info FILE.w16\n", argv[0]);
			return 1;
		}
		return cmd_info(argv[2]);
	}
	if (!strcmp(argv[1], "apply"))
		return cmd_apply(argc - 2, argv + 2);
	if (!strcmp(argv[1], "matrix"))
		return cmd_matrix(argc - 2, argv + 2);
	if (!strcmp(argv[1], "hex"))
		return cmd_hex(argc - 2, argv + 2);
	fprintf(stderr, "error: unknown command %s\n", argv[1]);
	usage(argv[0]);
	return 1;
}
