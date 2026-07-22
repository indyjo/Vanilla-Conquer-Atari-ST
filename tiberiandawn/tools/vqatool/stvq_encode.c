/*
 * stvq_encode.c - VQA → FORM 'STVQ' encoder.
 */
#include "stvq_encode.h"

#include "stvq_c2p.h"
#include "stvq_codebook.h"
#include "stvq_format.h"
#include "stvq_metric.h"
#include "stvq_palette.h"
#include "stvq_write.h"
#include "vqa_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t enc_ns_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void default_out_path(const char *vqa, char *out, size_t n)
{
	size_t len;
	const char *dot;
	snprintf(out, n, "%s", vqa);
	len = strlen(out);
	dot = strrchr(out, '.');
	if (dot && (size_t)(dot - out) + 5 < n) {
		memcpy((char *)dot, ".stv", 5);
	} else if (len + 4 < n) {
		memcpy(out + len, ".stv", 5);
	}
}

static int write_stpl_chunk(StvqWriter *w, const uint16_t stpl[16])
{
	unsigned char buf[32];
	int i;
	for (i = 0; i < 16; i++)
		stvq_write_be16(buf + i * 2, stpl[i]);
	return stvq_write_chunk_raw(w, STVQ_CHUNK_STPL, buf, 32);
}

static int write_stcb_chunk(StvqWriter *w, const StvqCodebook *cb)
{
	return stvq_write_chunk_raw(w, STVQ_CHUNK_STCB, cb->tiles, cb->entries * 32u);
}

static int write_stcr_chunk(StvqWriter *w, const StvqReplace *reps, unsigned n)
{
	unsigned char *buf;
	unsigned i;
	long size_pos;
	if (!n)
		return 0;
	buf = (unsigned char *)malloc(n * STVQ_STCR_ENTRY_BYTES);
	if (!buf)
		return -1;
	for (i = 0; i < n; i++) {
		stvq_write_be16(buf + i * 34u, reps[i].index);
		memcpy(buf + i * 34u + 2, reps[i].tile, 32);
	}
	if (stvq_write_chunk_begin(w, STVQ_CHUNK_STCR, &size_pos) != 0) {
		free(buf);
		return -1;
	}
	if (fwrite(buf, 1, n * 34u, w->fp) != n * 34u) {
		free(buf);
		return -1;
	}
	free(buf);
	return stvq_write_chunk_end(w, size_pos);
}

/*
 * N−2 skip: keep recon_n2 only if a codebook nearest would be a regression
 * (error(src,n2) <= error(src,cb)) and recon_n2 matches recon_n1 (field sync;
 * otherwise even/odd chains diverge and the block flickers at fps/2).
 * cb_idx / dist_cb come from STCR refresh (or a fallback assign).
 * Always writes the codebook index to *out_cb_idx.
 */
static int tile_can_skip(const uint8_t *src_vga, const uint8_t *recon_n2, const uint8_t *recon_n1,
    unsigned cb_idx, unsigned dist_cb, unsigned *out_cb_idx)
{
	unsigned dist_n2;
	if (out_cb_idx)
		*out_cb_idx = cb_idx;
	if (!recon_n2)
		return 0;
	if (recon_n1 && memcmp(recon_n2, recon_n1, 32) != 0)
		return 0;
	dist_n2 = stvq_src_tile_error(src_vga, recon_n2);
	return dist_n2 <= dist_cb;
}

static int write_stvd_chunk(StvqWriter *w, const StvqCodebook *cb, const uint8_t *src_tiles,
    const unsigned *cb_nearest, const unsigned *cb_dist, const uint8_t *recon_n2, const uint8_t *recon_n1,
    uint8_t *out_recon, unsigned tiles_x, unsigned tiles_y, int force_full, unsigned *out_bytes)
{
	long size_pos;
	unsigned col, row;
	unsigned bytes = 0;
	unsigned char tmp[8];
	uint16_t *idxs = NULL;
	uint8_t *skip = NULL;

	if (stvq_write_chunk_begin(w, STVQ_CHUNK_STVD, &size_pos) != 0)
		return -1;

	idxs = (uint16_t *)malloc((size_t)tiles_y * sizeof(uint16_t));
	skip = (uint8_t *)calloc(tiles_y, 1);
	if (!idxs || !skip) {
		free(idxs);
		free(skip);
		return -1;
	}

	for (col = 0; col < tiles_x; col++) {
		const uint8_t *col_src = src_tiles + (size_t)col * tiles_y * 64u;
		const uint8_t *n2_col = recon_n2 ? recon_n2 + (size_t)col * tiles_y * 32u : NULL;
		const uint8_t *n1_col = recon_n1 ? recon_n1 + (size_t)col * tiles_y * 32u : NULL;
		uint8_t *out_col = out_recon + (size_t)col * tiles_y * 32u;
		uint32_t mask = 0;

		memset(skip, 0, tiles_y);
		for (row = 0; row < tiles_y; row++) {
			unsigned ti = col * tiles_y + row;
			const uint8_t *src_vga = col_src + row * 64u;
			unsigned idx = 0;
			const uint8_t *n2 = (!force_full && n2_col) ? n2_col + row * 32u : NULL;
			const uint8_t *n1 = n1_col ? n1_col + row * 32u : NULL;
			if (tile_can_skip(src_vga, n2, n1, cb_nearest[ti], cb_dist[ti], &idx)) {
				skip[row] = 1;
				/* bit 31 = row 0, bit 30 = row 1, … (MSB-first for add/addx walk) */
				mask |= (1u << (31u - row));
			} else {
				idxs[row] = (uint16_t)idx;
			}
		}

		stvq_write_be32(tmp, mask);
		if (fwrite(tmp, 1, 4, w->fp) != 4) {
			free(idxs);
			free(skip);
			return -1;
		}
		bytes += 4;
		for (row = 0; row < tiles_y; row++) {
			uint8_t *dst = out_col + row * 32u;
			if (skip[row]) {
				memcpy(dst, n2_col + row * 32u, 32);
				continue;
			}
			stvq_write_be16(tmp, idxs[row]);
			if (fwrite(tmp, 1, 2, w->fp) != 2) {
				free(idxs);
				free(skip);
				return -1;
			}
			bytes += 2;
			memcpy(dst, cb->tiles + (size_t)idxs[row] * 32u, 32);
		}
	}

	free(idxs);
	free(skip);
	if (out_bytes)
		*out_bytes = bytes;
	return stvq_write_chunk_end(w, size_pos);
}

static void resample_frame_audio(const int16_t *pcm16, size_t pcm_n, unsigned src_rate, unsigned fps,
    unsigned frame_index, unsigned char **out, unsigned *out_n)
{
	/* Target cumulative samples at 12517 Hz */
	uint64_t t0 = ((uint64_t)frame_index * STVQ_SAMPLE_RATE + fps / 2u) / (fps ? fps : 15u);
	uint64_t t1 = ((uint64_t)(frame_index + 1u) * STVQ_SAMPLE_RATE + fps / 2u) / (fps ? fps : 15u);
	unsigned n = (unsigned)(t1 - t0);
	unsigned i;
	unsigned char *buf;

	*out = NULL;
	*out_n = 0;
	if (!n)
		return;
	/* Even sample count keeps SND0 word-aligned (IFF + native BE16 STVD). */
	if (n & 1u)
		n++;
	buf = (unsigned char *)malloc(n);
	if (!buf)
		return;
	/*
	 * pcm16 is this frame's slice only. Map output sample i ∈ [0,n) into that
	 * buffer — do not use absolute timeline positions (those clamp to the last
	 * sample for every frame after the first → silence + crackles).
	 */
	for (i = 0; i < n; i++) {
		int16_t s = 0;
		if (pcm_n) {
			size_t idx = (size_t)(((uint64_t)i * pcm_n) / n);
			if (idx >= pcm_n)
				idx = pcm_n - 1;
			s = pcm16[idx];
		}
		(void)src_rate;
		buf[i] = (unsigned char)((int)(s >> 8)); /* signed 8-bit */
	}
	*out = buf;
	*out_n = n;
}

int stvq_encode(const StvqEncodeOpts *opts)
{
	VqaDecode dec;
	StvqSegPalette *segpal = NULL;
	StvqC2P *c2ps = NULL;
	uint8_t **frame_tiles = NULL;
	uint8_t **frame_src = NULL;
	int *frame_seg = NULL;
	const uint8_t **seg_pal768 = NULL;
	const uint8_t **seg_subset = NULL;
	uint8_t *recon[2] = {NULL, NULL};
	uint8_t *recon_work = NULL;
	unsigned *cb_nearest = NULL;
	unsigned *cb_dist = NULL;
	StvqCodebook cb;
	StvqWriter w;
	StvqHeader hdr;
	char out_path[1024];
	unsigned tiles_x, tiles_y, tiles_n;
	unsigned f, s;
	unsigned max_frame = 0;
	const char *outp;
	int rc = -1;
	StvqSelectProf sel_prof;
	uint64_t prof_ns_raster = 0, prof_ns_train = 0, prof_ns_stvd = 0, prof_ns_frame_misc = 0;
	uint64_t prof_ns_frames = 0;

	memset(&dec, 0, sizeof(dec));
	memset(&cb, 0, sizeof(cb));
	memset(&w, 0, sizeof(w));
	memset(&sel_prof, 0, sizeof(sel_prof));

	if (!opts || !opts->vqa_path) {
		fprintf(stderr, "error: encode requires a VQA path\n");
		return -1;
	}

	{
		float alpha = opts->dct_alpha >= 0.0f ? opts->dct_alpha : STVQ_DEFAULT_DCT_ALPHA;
		unsigned nc = opts->dct_coeffs ? opts->dct_coeffs : STVQ_DEFAULT_DCT_COEFFS;
		unsigned nch = opts->have_dct_chroma ? opts->dct_chroma_coeffs : STVQ_DEFAULT_DCT_CHROMA_COEFFS;
		float gamma = opts->gamma >= 0.0f ? opts->gamma : STVQ_DEFAULT_GAMMA;
		stvq_metric_set_dct(alpha, nc, nch);
		stvq_metric_set_gamma(gamma);
	}

	fprintf(stderr, "decoding %s...\n", opts->vqa_path);
	if (vqa_decode_file(opts->vqa_path, &dec) != 0) {
		fprintf(stderr, "error: VQA decode failed\n");
		return -1;
	}

	tiles_x = stvq_tiles_x(dec.width);
	tiles_y = stvq_tiles_y(dec.height);
	tiles_n = tiles_x * tiles_y;
	if (tiles_y > 32u) {
		fprintf(stderr, "error: tiles_y=%u exceeds STVD mask width (32)\n", tiles_y);
		goto done;
	}

	fprintf(stderr, "frames=%u size=%ux%u tiles=%ux%u segments=%u cb=%u R=%u shortlist=%u*%u random=%u%% "
	                "lookahead=%u gamma=%.3g dct-alpha=%.3g dct-coeffs=%u+%u+%u (feat=%u)\n",
	    dec.frame_count, dec.width, dec.height, tiles_x, tiles_y, dec.segment_count, opts->cb_size,
	    opts->cb_per_frame, 2u, opts->cb_per_frame, opts->cb_random_pct, opts->cb_lookahead,
	    stvq_metric_gamma(), stvq_metric_dct_alpha(), stvq_metric_dct_coeffs(),
	    stvq_metric_dct_chroma_coeffs(), stvq_metric_dct_chroma_coeffs(), stvq_metric_feat_len());


	if (opts->dry_run) {
		for (s = 0; s < dec.segment_count; s++) {
			char pal[768], hist[768], w16[768];
			stvq_sidecar_paths(opts->vqa_path, (int)s, pal, hist, w16, sizeof(pal));
			fprintf(stderr, "  seg %u frames %d..%d → %s / %s / %s\n", s, dec.segments[s].start_frame,
			    dec.segments[s].end_frame, pal, hist, w16);
		}
		outp = opts->out_path;
		if (!outp) {
			default_out_path(opts->vqa_path, out_path, sizeof(out_path));
			outp = out_path;
		}
		fprintf(stderr, "  out: %s\n", outp);
		rc = 0;
		goto done;
	}

	segpal = (StvqSegPalette *)calloc(dec.segment_count, sizeof(*segpal));
	c2ps = (StvqC2P *)calloc(dec.segment_count, sizeof(*c2ps));
	frame_tiles = (uint8_t **)calloc(dec.frame_count, sizeof(uint8_t *));
	frame_src = (uint8_t **)calloc(dec.frame_count, sizeof(uint8_t *));
	frame_seg = (int *)calloc(dec.frame_count, sizeof(int));
	seg_pal768 = (const uint8_t **)calloc(dec.segment_count, sizeof(*seg_pal768));
	seg_subset = (const uint8_t **)calloc(dec.segment_count, sizeof(*seg_subset));
	if (!segpal || !c2ps || !frame_tiles || !frame_src || !frame_seg || !seg_pal768 || !seg_subset)
		goto done;

	for (s = 0; s < dec.segment_count; s++) {
		if (stvq_load_segment_w16(opts->vqa_path, (int)s, &dec.segments[s], &segpal[s]) != 0)
			goto done;
		stvq_c2p_init(&c2ps[s], &segpal[s].w16);
		seg_pal768[s] = dec.segments[s].pal;
		seg_subset[s] = segpal[s].w16.subset;
	}
	for (f = 0; f < dec.frame_count; f++)
		frame_seg[f] = dec.frames[f].segment;

	fprintf(stderr, "rasterizing tiles...\n");
	{
		uint64_t t0 = enc_ns_now();
		for (f = 0; f < dec.frame_count; f++) {
			int seg = dec.frames[f].segment;
			frame_tiles[f] = (uint8_t *)malloc((size_t)tiles_n * 32u);
			frame_src[f] = (uint8_t *)malloc((size_t)tiles_n * 64u);
			if (!frame_tiles[f] || !frame_src[f])
				goto done;
			if (stvq_frame_to_tiles(&c2ps[seg], dec.frames[f].pixels, dec.width, dec.height, tiles_x,
			        tiles_y, frame_tiles[f], frame_src[f]) != 0)
				goto done;
		}
		prof_ns_raster = enc_ns_now() - t0;
	}

	fprintf(stderr, "training codebook (%u)...\n", opts->cb_size);
	stvq_metric_set_palette_vga6(dec.segments[0].pal, segpal[0].w16.subset);
	if (stvq_codebook_alloc(&cb, opts->cb_size) != 0)
		goto done;
	{
		unsigned stride = 1;
		uint64_t t0 = enc_ns_now();
		if (dec.frame_count * tiles_n > 200000u)
			stride = (dec.frame_count * tiles_n) / 100000u;
		if (stvq_codebook_train(&cb, (const uint8_t *const *)frame_tiles, (const uint8_t *const *)frame_src,
		        dec.frame_count, tiles_n, stride) != 0)
			goto done;
		prof_ns_train = enc_ns_now() - t0;
	}

	recon[0] = (uint8_t *)calloc(tiles_n, 32u);
	recon[1] = (uint8_t *)calloc(tiles_n, 32u);
	recon_work = (uint8_t *)calloc(tiles_n, 32u);
	cb_nearest = (unsigned *)malloc((size_t)tiles_n * sizeof(unsigned));
	cb_dist = (unsigned *)malloc((size_t)tiles_n * sizeof(unsigned));
	if (!recon[0] || !recon[1] || !recon_work || !cb_nearest || !cb_dist)
		goto done;

	outp = opts->out_path;
	if (!outp) {
		default_out_path(opts->vqa_path, out_path, sizeof(out_path));
		outp = out_path;
	}
	fprintf(stderr, "writing %s...\n", outp);
	if (stvq_writer_open(&w, outp) != 0)
		goto done;
	if (stvq_write_form_begin(&w) != 0)
		goto done;

	memset(&hdr, 0, sizeof(hdr));
	hdr.version = STVQ_VERSION;
	hdr.flags = (dec.hdr.flags & 1u) ? 1u : 0u;
	hdr.frames = (uint16_t)dec.frame_count;
	hdr.width = (uint16_t)dec.width;
	hdr.height = (uint16_t)dec.height;
	hdr.block_w = 8;
	hdr.block_h = 8;
	hdr.fps = dec.hdr.fps ? dec.hdr.fps : 15;
	hdr.cb_entries = (uint16_t)opts->cb_size;
	hdr.sample_rate = (uint16_t)STVQ_SAMPLE_RATE;
	hdr.channels = 1;
	hdr.bits_per_sample = 8;
	{
		unsigned char raw[STVQ_STHD_SIZE];
		stvq_header_pack(raw, &hdr);
		if (stvq_write_chunk_raw(&w, STVQ_CHUNK_STHD, raw, STVQ_STHD_SIZE) != 0)
			goto done;
	}

	if (write_stpl_chunk(&w, segpal[0].stpl) != 0)
		goto done;
	if (write_stcb_chunk(&w, &cb) != 0)
		goto done;

	for (f = 0; f < dec.frame_count; f++) {
		long fr_pos;
		StvqReplace *reps = NULL;
		unsigned nrep = 0;
		unsigned stvd_bytes = 0;
		unsigned char *snd = NULL;
		unsigned snd_n = 0;
		int force_full = (f < 2);
		const uint8_t *recon_n2 = (f >= 2) ? recon[1] : NULL;
		const uint8_t *recon_n1 = (f >= 1) ? recon[0] : NULL;
		int seg = dec.frames[f].segment;
		int emit_stpl = 0;
		uint64_t frame_t0 = enc_ns_now(), t0, t1;

		/* New W16/palette → rebuild CB features; prior tiles become prime eviction. */
		stvq_metric_set_palette_vga6(dec.segments[seg].pal, segpal[seg].w16.subset);
		stvq_codebook_recompute_feats(&cb);

		if (f > 0 && dec.frames[f].segment != dec.frames[f - 1].segment) {
			emit_stpl = 1;
			stvq_codebook_on_palette_change(&cb);
			/* Pre-cut recon uses the old STPL; do not skip or stay-as-is. */
			force_full = 1;
			recon_n2 = NULL;
			recon_n1 = NULL;
		}

		if (stvq_write_chunk_begin(&w, STVQ_CHUNK_STFR, &fr_pos) != 0)
			goto done;

		if (emit_stpl) {
			if (write_stpl_chunk(&w, segpal[seg].stpl) != 0)
				goto done;
		}

		if (opts->cb_per_frame) {
			reps = (StvqReplace *)malloc(opts->cb_per_frame * sizeof(*reps));
			if (!reps)
				goto done;
			nrep = stvq_codebook_select_replaces(&cb, (const uint8_t *const *)frame_tiles,
			    (const uint8_t *const *)frame_src, dec.frame_count, f, tiles_n, opts->cb_per_frame,
			    opts->cb_random_pct, opts->cb_lookahead, recon_n2, recon_n1, frame_seg, seg_pal768,
			    seg_subset, reps, &sel_prof, cb_nearest, cb_dist);
			if (write_stcr_chunk(&w, reps, nrep) != 0) {
				free(reps);
				goto done;
			}
			free(reps);
			reps = NULL;
		} else {
			unsigned ti;
			for (ti = 0; ti < tiles_n; ti++)
				cb_nearest[ti] = stvq_codebook_nearest(&cb, frame_src[f] + ti * 64u, &cb_dist[ti]);
		}

		t0 = enc_ns_now();
		if (write_stvd_chunk(&w, &cb, frame_src[f], cb_nearest, cb_dist, recon_n2, recon_n1, recon_work,
		        tiles_x, tiles_y, force_full, &stvd_bytes) != 0)
			goto done;
		t1 = enc_ns_now();
		prof_ns_stvd += t1 - t0;

		/* Ping-pong: recon[1] ← old recon[0] (becomes N−2 next); recon[0] ← new */
		memcpy(recon[1], recon[0], (size_t)tiles_n * 32u);
		memcpy(recon[0], recon_work, (size_t)tiles_n * 32u);

		resample_frame_audio(dec.frames[f].pcm16, dec.frames[f].pcm16_count, dec.sample_rate, hdr.fps, f,
		    &snd, &snd_n);
		if (snd_n) {
			if (stvq_write_chunk_raw(&w, STVQ_CHUNK_SND0, snd, snd_n) != 0) {
				free(snd);
				goto done;
			}
		}
		free(snd);

		if (stvq_write_chunk_end(&w, fr_pos) != 0)
			goto done;

		{
			long end = ftell(w.fp);
			unsigned fb = (unsigned)(end > fr_pos ? end - fr_pos : 0);
			if (fb > max_frame)
				max_frame = fb;
			(void)stvd_bytes;
		}

		prof_ns_frames += enc_ns_now() - frame_t0;

		if ((f % 50u) == 0u)
			fprintf(stderr, "  frame %u/%u\n", f, dec.frame_count);
	}

	if (stvq_write_chunk_raw(&w, STVQ_CHUNK_STEN, NULL, 0) != 0)
		goto done;
	if (stvq_write_form_end(&w) != 0)
		goto done;

	/* patch max_frame_bytes in STHD */
	{
		unsigned char be[2];
		long sthd_data = 12 + 8; /* FORM hdr + STHD hdr */
		stvq_write_be16(be, (uint16_t)(max_frame > 0xffffu ? 0xffffu : max_frame));
		if (fseek(w.fp, sthd_data + 20, SEEK_SET) == 0)
			fwrite(be, 1, 2, w.fp);
	}

	if (stvq_writer_close(&w) != 0)
		goto done;
	w.fp = NULL;
	fprintf(stderr, "done: %s (max_frame≈%u)\n", outp, max_frame);
	{
		uint64_t stcr = sel_prof.ns_refresh + sel_prof.ns_residual + sel_prof.ns_utility + sel_prof.ns_random;
		uint64_t accounted = stcr + prof_ns_stvd;
		uint64_t misc = (prof_ns_frames > accounted) ? (prof_ns_frames - accounted) : 0;
		double nf = dec.frame_count ? (double)dec.frame_count : 1.0;
		double frame_ms = (prof_ns_frames / nf) / 1e6;
		fprintf(stderr, "profile (avg per frame, writing loop only):\n");
		fprintf(stderr, "  total writing/frame  %6.2f ms\n", frame_ms);
		fprintf(stderr, "  STCR refresh         %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_refresh / nf) / 1e6, 100.0 * sel_prof.ns_refresh / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "  STCR residual        %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_residual / nf) / 1e6, 100.0 * sel_prof.ns_residual / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "  STCR utility         %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_utility / nf) / 1e6, 100.0 * sel_prof.ns_utility / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "    util score         %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_util_score / nf) / 1e6,
		    100.0 * sel_prof.ns_util_score / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "    util install       %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_util_install / nf) / 1e6,
		    100.0 * sel_prof.ns_util_install / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "    victim d_new       %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_victim_dnew / nf) / 1e6,
		    100.0 * sel_prof.ns_victim_dnew / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "    victim scan        %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_victim_scan / nf) / 1e6,
		    100.0 * sel_prof.ns_victim_scan / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "  STCR random          %6.2f ms  %5.1f%%\n",
		    (sel_prof.ns_random / nf) / 1e6, 100.0 * sel_prof.ns_random / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "  STVD encode          %6.2f ms  %5.1f%%\n",
		    (prof_ns_stvd / nf) / 1e6, 100.0 * prof_ns_stvd / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "  frame misc (I/O+snd) %6.2f ms  %5.1f%%\n", (misc / nf) / 1e6,
		    100.0 * misc / (prof_ns_frames ? prof_ns_frames : 1));
		fprintf(stderr, "profile (one-time setup):\n");
		fprintf(stderr, "  rasterize            %6.2f s\n", prof_ns_raster / 1e9);
		fprintf(stderr, "  train codebook       %6.2f s\n", prof_ns_train / 1e9);
		(void)prof_ns_frame_misc;
	}
	rc = 0;

done:
	if (w.fp)
		stvq_writer_close(&w);
	stvq_codebook_free(&cb);
	if (frame_tiles) {
		for (f = 0; f < dec.frame_count; f++)
			free(frame_tiles[f]);
		free(frame_tiles);
	}
	if (frame_src) {
		for (f = 0; f < dec.frame_count; f++)
			free(frame_src[f]);
		free(frame_src);
	}
	free(recon[0]);
	free(recon[1]);
	free(recon_work);
	free(cb_nearest);
	free(cb_dist);
	free(frame_seg);
	free((void *)seg_pal768);
	free((void *)seg_subset);
	free(c2ps);
	free(segpal);
	vqa_decode_free(&dec);
	return rc;
}
