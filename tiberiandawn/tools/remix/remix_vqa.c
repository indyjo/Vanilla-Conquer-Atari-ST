/*
 * remix_vqa.c - VQA → STVQ via vqatool encode, CRC-named video/ W16 sidecars.
 */
#define _POSIX_C_SOURCE 200809L

#include "remix_vqa.h"

#include "stvq_encode.h"
#include "stvq_format.h"
#include "stvq_palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int remix_vqa_is_stvq(const unsigned char *data, size_t len)
{
	uint32_t form_id;
	uint32_t type_id;

	if (len < 12)
		return 0;
	form_id = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16)
	    | ((uint32_t)data[3] << 24);
	type_id = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16)
	    | ((uint32_t)data[11] << 24);
	return form_id == STVQ_CHUNK_FORM && type_id == STVQ_CHUNK_STVQ;
}

static unsigned cb_per_frame_for_quality(RemixVideoQuality q)
{
	switch (q) {
	case REMIX_VIDEO_QUALITY_LOW:
		return 32u;
	case REMIX_VIDEO_QUALITY_HIGH:
		return 128u;
	case REMIX_VIDEO_QUALITY_MEDIUM:
	default:
		return 64u;
	}
}

static void dct_for_effort(RemixVideoEffort e, unsigned *y, unsigned *chroma)
{
	switch (e) {
	case REMIX_VIDEO_EFFORT_FAST:
		*y = 32u;
		*chroma = 16u;
		break;
	case REMIX_VIDEO_EFFORT_THOROUGH:
		*y = 64u;
		*chroma = 64u;
		break;
	case REMIX_VIDEO_EFFORT_NORMAL:
	default:
		*y = 48u;
		*chroma = 40u;
		break;
	}
}

/** Extra frames after the current for STCR utility/eviction (0 = current frame only). */
static unsigned lookahead_for_effort(RemixVideoEffort e)
{
	switch (e) {
	case REMIX_VIDEO_EFFORT_FAST:
		return 0u;
	case REMIX_VIDEO_EFFORT_THOROUGH:
		return 3u;
	case REMIX_VIDEO_EFFORT_NORMAL:
	default:
		return 1u;
	}
}

static int write_payload_temp(
    FILE *in, long in_pos, const unsigned char *probe, size_t probe_len, uint32_t payload_size,
    const char *path)
{
	FILE *tmp;
	size_t remain;

	tmp = fopen(path, "wb");
	if (!tmp)
		return 0;
	if (fwrite(probe, 1, probe_len, tmp) != probe_len) {
		fclose(tmp);
		return 0;
	}
	remain = payload_size - (uint32_t)probe_len;
	if (remain > 0) {
		unsigned char chunk[REMIX_COPY_CHUNK];
		if (fseek(in, in_pos + (long)probe_len, SEEK_SET) != 0) {
			fclose(tmp);
			return 0;
		}
		while (remain > 0) {
			size_t n = remain > sizeof(chunk) ? sizeof(chunk) : remain;
			if (fread(chunk, 1, n, in) != n || fwrite(chunk, 1, n, tmp) != n) {
				fclose(tmp);
				return 0;
			}
			remain -= n;
		}
	}
	if (fclose(tmp) != 0)
		return 0;
	return 1;
}

static int write_buffer_temp(const unsigned char *data, size_t len, const char *path)
{
	FILE *tmp;

	tmp = fopen(path, "wb");
	if (!tmp)
		return 0;
	if (len > 0 && fwrite(data, 1, len, tmp) != len) {
		fclose(tmp);
		return 0;
	}
	if (fclose(tmp) != 0)
		return 0;
	return 1;
}

static int copy_file_to(FILE *out, const char *path, uint32_t *out_size)
{
	FILE *fp;
	unsigned char chunk[REMIX_COPY_CHUNK];
	uint32_t total = 0;

	fp = fopen(path, "rb");
	if (!fp)
		return 0;
	for (;;) {
		size_t n = fread(chunk, 1, sizeof(chunk), fp);
		if (n == 0)
			break;
		if (fwrite(chunk, 1, n, out) != n) {
			fclose(fp);
			return 0;
		}
		total += (uint32_t)n;
	}
	if (ferror(fp)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	*out_size = total;
	return 1;
}

static int read_file_alloc(const char *path, unsigned char **out_buf, uint32_t *out_len)
{
	FILE *fp;
	long n;
	unsigned char *buf;

	fp = fopen(path, "rb");
	if (!fp)
		return 0;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return 0;
	}
	n = ftell(fp);
	if (n < 0) {
		fclose(fp);
		return 0;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return 0;
	}
	buf = (unsigned char *)malloc((size_t)n ? (size_t)n : 1u);
	if (!buf) {
		fclose(fp);
		return 0;
	}
	if (n > 0 && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
		free(buf);
		fclose(fp);
		return 0;
	}
	fclose(fp);
	*out_buf = buf;
	*out_len = (uint32_t)n;
	return 1;
}

static int any_crc_w16_present(const RemixConfig *cfg, uint32_t crc)
{
	char path[768];
	if (stvq_crc_w16_path(cfg->w16_dir, crc, 0, path, sizeof(path)) != 0)
		return 0;
	return access(path, R_OK) == 0;
}

typedef struct VqaProgressCtx {
	const RemixConfig *cfg;
	uint32_t crc;
} VqaProgressCtx;

static void vqa_stvq_progress(void *ctx, const char *phase, unsigned done, unsigned total)
{
	VqaProgressCtx *p = (VqaProgressCtx *)ctx;

	if (!p || !p->cfg || !p->cfg->encode_progress)
		return;
	p->cfg->encode_progress(p->cfg->encode_progress_ctx, phase, p->crc, done, total);
}

/**
 * Encode VQA file at vqa_path → stv_path. Returns 1 / -1 / 0.
 * Unlinks vqa_path before return (always). Unlinks stv_path on failure.
 */
static int encode_vqa_file(
    const char *vqa_path, const char *stv_path, uint32_t crc, uint32_t payload_size,
    const RemixConfig *cfg)
{
	StvqEncodeOpts opts;
	unsigned dct_y, dct_c;
	int enc_rc;
	VqaProgressCtx prog;

	memset(&opts, 0, sizeof(opts));
	opts.vqa_path = vqa_path;
	opts.out_path = stv_path;
	opts.cb_size = STVQ_DEFAULT_CB_SIZE;
	opts.cb_per_frame = cb_per_frame_for_quality(cfg->video_quality);
	opts.cb_random_pct = STVQ_DEFAULT_CB_RANDOM_PCT;
	opts.cb_lookahead = lookahead_for_effort(cfg->video_effort);
	opts.dct_alpha = -1.0f;
	opts.gamma = -1.0f;
	dct_for_effort(cfg->video_effort, &dct_y, &dct_c);
	opts.dct_coeffs = dct_y;
	opts.dct_chroma_coeffs = dct_c;
	opts.have_dct_chroma = 1;
	opts.w16_dir = cfg->w16_dir;
	opts.w16_crc = crc;
	opts.have_w16_crc = 1;
	prog.cfg = cfg;
	prog.crc = crc;
	opts.progress = vqa_stvq_progress;
	opts.progress_ctx = &prog;

	/* Notify UI/CLI before the (slow) encode so the entry appears like other conversions. */
	if (cfg->encode_progress)
		cfg->encode_progress(cfg->encode_progress_ctx, "start", crc, payload_size, 0);
	fprintf(stderr, "%08X %u vqa → stv (cb/frame=%u dct=%u/%u lookahead=%u)\n", (unsigned)crc,
	    (unsigned)payload_size, opts.cb_per_frame, dct_y, dct_c, opts.cb_lookahead);
	enc_rc = stvq_encode(&opts);
	unlink(vqa_path);
	if (enc_rc != 0) {
		fprintf(stderr, "warning: omit %08x — STVQ encode failed\n", (unsigned)crc);
		unlink(stv_path);
		return -1;
	}
	return 1;
}

int remix_vqa_convert_payload(
    FILE *in, long in_pos, const unsigned char *probe, size_t probe_len, uint32_t payload_size,
    uint32_t crc, const RemixConfig *cfg, FILE *out, uint32_t *out_size)
{
	char vqa_path[] = "/tmp/remix-vqa-XXXXXX";
	char stv_path[sizeof(vqa_path) + 8];
	int fd;
	int enc_rc;

	if (!cfg || !out || !out_size)
		return 0;

	if (!any_crc_w16_present(cfg, crc)) {
		fprintf(stderr, "warning: omit %08x — missing video/%08x.*.w16\n", (unsigned)crc,
		    (unsigned)crc);
		return -1;
	}

	fd = mkstemp(vqa_path);
	if (fd < 0)
		return 0;
	close(fd);
	/* mkstemp already created an empty file; overwrite with VQA payload. */
	snprintf(stv_path, sizeof(stv_path), "%s.stv", vqa_path);

	if (!write_payload_temp(in, in_pos, probe, probe_len, payload_size, vqa_path)) {
		unlink(vqa_path);
		return 0;
	}

	enc_rc = encode_vqa_file(vqa_path, stv_path, crc, payload_size, cfg);
	if (enc_rc != 1)
		return enc_rc;

	if (!copy_file_to(out, stv_path, out_size)) {
		unlink(stv_path);
		return 0;
	}
	unlink(stv_path);
	return 1;
}

int remix_vqa_convert_buffer(
    const unsigned char *vqa, size_t vqa_len, uint32_t crc, const RemixConfig *cfg,
    unsigned char **out_stv, uint32_t *out_len)
{
	char vqa_path[] = "/tmp/remix-vqa-XXXXXX";
	char stv_path[sizeof(vqa_path) + 8];
	int fd;
	int enc_rc;

	if (!cfg || !vqa || !out_stv || !out_len)
		return 0;
	*out_stv = NULL;
	*out_len = 0;

	if (!any_crc_w16_present(cfg, crc)) {
		fprintf(stderr, "warning: omit %08x — missing video/%08x.*.w16\n", (unsigned)crc,
		    (unsigned)crc);
		return -1;
	}

	fd = mkstemp(vqa_path);
	if (fd < 0)
		return 0;
	close(fd);
	snprintf(stv_path, sizeof(stv_path), "%s.stv", vqa_path);

	if (!write_buffer_temp(vqa, vqa_len, vqa_path)) {
		unlink(vqa_path);
		return 0;
	}

	enc_rc = encode_vqa_file(vqa_path, stv_path, crc, (uint32_t)vqa_len, cfg);
	if (enc_rc != 1)
		return enc_rc;

	if (!read_file_alloc(stv_path, out_stv, out_len)) {
		unlink(stv_path);
		return 0;
	}
	unlink(stv_path);
	return 1;
}
