/*
 * vqa_dump.c - Write decoded VQA frames as uncompressed BMP files.
 */
#include "vqa_dump.h"

#include "vqa_decode.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint8_t vga6_to_8(uint8_t c)
{
	c &= 63u;
	return (uint8_t)((c << 2) | (c >> 4));
}

static void stem_and_dir(const char *vqa_path, char *dir, size_t dir_n, char *stem, size_t stem_n)
{
	const char *slash = strrchr(vqa_path, '/');
	const char *base = slash ? slash + 1 : vqa_path;
	char *dot;

	if (!slash) {
		snprintf(dir, dir_n, ".");
	} else {
		size_t len = (size_t)(slash - vqa_path);
		if (len >= dir_n)
			len = dir_n - 1;
		memcpy(dir, vqa_path, len);
		dir[len] = '\0';
	}
	snprintf(stem, stem_n, "%s", base);
	dot = strrchr(stem, '.');
	if (dot)
		*dot = '\0';
}

static int ensure_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return 0;
		fprintf(stderr, "error: %s exists and is not a directory\n", path);
		return -1;
	}
	if (mkdir(path, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "error: mkdir %s: %s\n", path, strerror(errno));
		return -1;
	}
	return 0;
}

static int write_le16(FILE *fp, uint16_t v)
{
	unsigned char b[2] = {(unsigned char)(v & 0xffu), (unsigned char)((v >> 8) & 0xffu)};
	return fwrite(b, 1, 2, fp) == 2 ? 0 : -1;
}

static int write_le32(FILE *fp, uint32_t v)
{
	unsigned char b[4] = {(unsigned char)(v & 0xffu), (unsigned char)((v >> 8) & 0xffu),
	    (unsigned char)((v >> 16) & 0xffu), (unsigned char)((v >> 24) & 0xffu)};
	return fwrite(b, 1, 4, fp) == 4 ? 0 : -1;
}

/*
 * Uncompressed Windows BMP.
 * rgb24=0: 8-bit indexed, palette from segment VGA6 (expanded to 8-bit).
 * rgb24=1: 24-bit BGR, no palette.
 * Rows stored bottom-up; stride padded to 4 bytes.
 */
static int write_bmp(const char *path, unsigned width, unsigned height, const unsigned char *pixels,
    const unsigned char pal768[768], int rgb24)
{
	FILE *fp;
	unsigned stride;
	unsigned row;
	uint32_t info_size = 40;
	uint32_t colors = rgb24 ? 0u : 256u;
	uint32_t off_bits = 14u + info_size + colors * 4u;
	uint32_t img_size;
	uint32_t file_size;
	unsigned i;

	if (rgb24)
		stride = (width * 3u + 3u) & ~3u;
	else
		stride = (width + 3u) & ~3u;
	img_size = stride * height;
	file_size = off_bits + img_size;

	fp = fopen(path, "wb");
	if (!fp) {
		fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
		return -1;
	}

	/* BITMAPFILEHEADER */
	if (fputc('B', fp) == EOF || fputc('M', fp) == EOF || write_le32(fp, file_size) != 0 ||
	    write_le16(fp, 0) != 0 || write_le16(fp, 0) != 0 || write_le32(fp, off_bits) != 0) {
		fclose(fp);
		return -1;
	}
	/* BITMAPINFOHEADER */
	if (write_le32(fp, info_size) != 0 || write_le32(fp, width) != 0 || write_le32(fp, height) != 0 ||
	    write_le16(fp, 1) != 0 || write_le16(fp, rgb24 ? 24u : 8u) != 0 || write_le32(fp, 0) != 0 ||
	    write_le32(fp, img_size) != 0 || write_le32(fp, 0) != 0 || write_le32(fp, 0) != 0 ||
	    write_le32(fp, colors) != 0 || write_le32(fp, colors) != 0) {
		fclose(fp);
		return -1;
	}

	if (!rgb24) {
		for (i = 0; i < 256; i++) {
			unsigned char bgrx[4];
			bgrx[0] = vga6_to_8(pal768[i * 3u + 2u]);
			bgrx[1] = vga6_to_8(pal768[i * 3u + 1u]);
			bgrx[2] = vga6_to_8(pal768[i * 3u + 0u]);
			bgrx[3] = 0;
			if (fwrite(bgrx, 1, 4, fp) != 4) {
				fclose(fp);
				return -1;
			}
		}
	}

	for (row = 0; row < height; row++) {
		unsigned y = height - 1u - row;
		const unsigned char *src = pixels + (size_t)y * width;
		if (rgb24) {
			unsigned char *line = (unsigned char *)calloc(1, stride);
			unsigned x;
			if (!line) {
				fclose(fp);
				return -1;
			}
			for (x = 0; x < width; x++) {
				unsigned idx = src[x];
				line[x * 3u + 0u] = vga6_to_8(pal768[idx * 3u + 2u]);
				line[x * 3u + 1u] = vga6_to_8(pal768[idx * 3u + 1u]);
				line[x * 3u + 2u] = vga6_to_8(pal768[idx * 3u + 0u]);
			}
			if (fwrite(line, 1, stride, fp) != stride) {
				free(line);
				fclose(fp);
				return -1;
			}
			free(line);
		} else {
			unsigned char pad[4] = {0, 0, 0, 0};
			unsigned pad_n = stride - width;
			if (fwrite(src, 1, width, fp) != width) {
				fclose(fp);
				return -1;
			}
			if (pad_n && fwrite(pad, 1, pad_n, fp) != pad_n) {
				fclose(fp);
				return -1;
			}
		}
	}

	if (fclose(fp) != 0)
		return -1;
	return 0;
}

int vqa_dump(const VqaDumpOpts *opts)
{
	VqaDecode dec;
	char vqa_dir[512], stem[256], out_dir[768], path[1024];
	int first, last, every, f, written = 0;
	int rc = -1;

	memset(&dec, 0, sizeof(dec));
	if (!opts || !opts->vqa_path) {
		fprintf(stderr, "error: dump requires a VQA path\n");
		return -1;
	}

	every = opts->every > 0 ? opts->every : 1;
	stem_and_dir(opts->vqa_path, vqa_dir, sizeof(vqa_dir), stem, sizeof(stem));
	if (opts->out_dir && opts->out_dir[0])
		snprintf(out_dir, sizeof(out_dir), "%s", opts->out_dir);
	else
		snprintf(out_dir, sizeof(out_dir), "%s/%s_bmp", vqa_dir, stem);

	fprintf(stderr, "decoding %s...\n", opts->vqa_path);
	if (vqa_decode_file(opts->vqa_path, &dec) != 0) {
		fprintf(stderr, "error: VQA decode failed\n");
		return -1;
	}

	first = opts->frame_first >= 0 ? opts->frame_first : 0;
	last = opts->frame_last >= 0 ? opts->frame_last : (int)dec.frame_count - 1;
	if (first < 0)
		first = 0;
	if (last >= (int)dec.frame_count)
		last = (int)dec.frame_count - 1;
	if (first > last) {
		fprintf(stderr, "error: empty frame range %d..%d\n", first, last);
		goto done;
	}

	fprintf(stderr, "frames=%u size=%ux%u segments=%u dump %d..%d every %d → %s (%s)\n",
	    dec.frame_count, dec.width, dec.height, dec.segment_count, first, last, every, out_dir,
	    opts->rgb24 ? "rgb24" : "8-bit paletted");

	if (opts->dry_run) {
		rc = 0;
		goto done;
	}

	if (ensure_dir(out_dir) != 0)
		goto done;

	for (f = first; f <= last; f++) {
		const VqaDecodedFrame *fr;
		const unsigned char *pal;
		if ((f - first) % every != 0)
			continue;
		fr = &dec.frames[f];
		if (!fr->pixels) {
			fprintf(stderr, "error: frame %d has no pixels\n", f);
			goto done;
		}
		if (fr->segment < 0 || (unsigned)fr->segment >= dec.segment_count) {
			fprintf(stderr, "error: frame %d bad segment %d\n", f, fr->segment);
			goto done;
		}
		pal = dec.segments[fr->segment].pal;
		snprintf(path, sizeof(path), "%s/%s_%04d.bmp", out_dir, stem, f);
		if (write_bmp(path, dec.width, dec.height, fr->pixels, pal, opts->rgb24) != 0)
			goto done;
		written++;
		if ((written % 50) == 0 || f == last)
			fprintf(stderr, "  wrote %d file(s) (frame %d)\n", written, f);
	}

	fprintf(stderr, "done: %d BMP(s) in %s\n", written, out_dir);
	rc = 0;

done:
	vqa_decode_free(&dec);
	return rc;
}
