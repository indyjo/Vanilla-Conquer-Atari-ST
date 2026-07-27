/*
 * stvq_palette.c - Sidecar pal/hist/w16 load/write helpers.
 */
#include "stvq_palette.h"

#include "stvq_format.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void stem_from_vqa(const char *vqa_path, char *stem, size_t n)
{
	const char *base = strrchr(vqa_path, '/');
	char *dot;
	base = base ? base + 1 : vqa_path;
	snprintf(stem, n, "%s", base);
	dot = strrchr(stem, '.');
	if (dot)
		*dot = '\0';
}

static void dir_from_vqa(const char *vqa_path, char *dir, size_t n)
{
	const char *slash = strrchr(vqa_path, '/');
	if (!slash) {
		snprintf(dir, n, ".");
		return;
	}
	{
		size_t len = (size_t)(slash - vqa_path);
		if (len >= n)
			len = n - 1;
		memcpy(dir, vqa_path, len);
		dir[len] = '\0';
	}
}

int stvq_sidecar_paths(const char *vqa_path, int seg, char *pal, char *hist, char *w16, size_t n)
{
	char dir[512];
	char stem[256];
	dir_from_vqa(vqa_path, dir, sizeof(dir));
	stem_from_vqa(vqa_path, stem, sizeof(stem));
	snprintf(pal, n, "%s/%s.%d.pal", dir, stem, seg);
	snprintf(hist, n, "%s/%s.%d.hist", dir, stem, seg);
	snprintf(w16, n, "%s/%s.%d.w16", dir, stem, seg);
	return 0;
}

int stvq_crc_w16_path(const char *w16_dir, uint32_t crc, int seg, char *w16, size_t n)
{
	const char *root = (w16_dir && w16_dir[0]) ? w16_dir : ".";
	if (snprintf(w16, n, "%s/video/%08x.%d.w16", root, (unsigned)crc, seg) >= (int)n)
		return -1;
	return 0;
}

int stvq_write_pal(const char *path, const unsigned char pal[768])
{
	FILE *fp = fopen(path, "wb");
	if (!fp) {
		fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (fwrite(pal, 1, 768, fp) != 768) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

int stvq_write_hist(const char *path, const uint64_t counts[256])
{
	FILE *fp = fopen(path, "w");
	int i;
	if (!fp) {
		fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
		return -1;
	}
	for (i = 0; i < 256; i++) {
		if (counts[i])
			fprintf(fp, "%d %llu\n", i, (unsigned long long)counts[i]);
	}
	fclose(fp);
	return 0;
}

int stvq_load_w16(const char *path, StvqWeightSet *out)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (fread(out, 1, sizeof(*out), fp) != sizeof(*out)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	if (memcmp(out->magic, STVQ_W16_MAGIC, 3) != 0) {
		fprintf(stderr, "error: %s: bad W16 magic\n", path);
		return -1;
	}
	return 0;
}

int stvq_build_stpl(const StvqWeightSet *w16, const unsigned char pal[768], uint16_t stpl_be[16])
{
	int i;
	for (i = 0; i < 16; i++) {
		unsigned idx = w16->subset[i];
		uint8_t r = (uint8_t)(pal[idx * 3 + 0] & 63u);
		uint8_t g = (uint8_t)(pal[idx * 3 + 1] & 63u);
		uint8_t b = (uint8_t)(pal[idx * 3 + 2] & 63u);
		stpl_be[i] = stvq_vga6_to_ste(r, g, b);
	}
	return 0;
}

static void fill_pen_vga6(const StvqWeightSet *w16, const unsigned char pal[768], uint8_t out[48])
{
	int i;
	for (i = 0; i < 16; i++) {
		unsigned idx = w16->subset[i];
		out[i * 3 + 0] = (uint8_t)(pal[idx * 3 + 0] & 63u);
		out[i * 3 + 1] = (uint8_t)(pal[idx * 3 + 1] & 63u);
		out[i * 3 + 2] = (uint8_t)(pal[idx * 3 + 2] & 63u);
	}
}

static int file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int load_segment_w16_from_path(
    const char *w16_path, const VqaPalSegment *seginfo, StvqSegPalette *out)
{
	memset(out, 0, sizeof(*out));
	if (!file_exists(w16_path)) {
		fprintf(stderr, "error: missing %s (run: vqatool init-w16 ...)\n", w16_path);
		return -1;
	}
	if (stvq_load_w16(w16_path, &out->w16) != 0)
		return -1;
	if (stvq_build_stpl(&out->w16, seginfo->pal, out->stpl) != 0)
		return -1;
	fill_pen_vga6(&out->w16, seginfo->pal, out->pen_vga6);
	out->have_w16 = 1;
	return 0;
}

int stvq_load_segment_w16(const char *vqa_path, int seg, const VqaPalSegment *seginfo, StvqSegPalette *out)
{
	char pal_path[768], hist_path[768], w16_path[768];
	stvq_sidecar_paths(vqa_path, seg, pal_path, hist_path, w16_path, sizeof(pal_path));
	(void)pal_path;
	(void)hist_path;
	return load_segment_w16_from_path(w16_path, seginfo, out);
}

int stvq_load_segment_w16_crc(
    const char *w16_dir, uint32_t crc, int seg, const VqaPalSegment *seginfo, StvqSegPalette *out)
{
	char w16_path[768];
	if (stvq_crc_w16_path(w16_dir, crc, seg, w16_path, sizeof(w16_path)) != 0)
		return -1;
	return load_segment_w16_from_path(w16_path, seginfo, out);
}
