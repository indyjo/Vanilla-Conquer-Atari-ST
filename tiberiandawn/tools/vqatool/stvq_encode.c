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
#include "vqa_format.h"
#include "st_host_resample.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_stvq_encode_error[256];

const char *stvq_encode_error(void)
{
	return g_stvq_encode_error;
}

static void stvq_encode_set_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(g_stvq_encode_error, sizeof(g_stvq_encode_error), fmt, ap);
	va_end(ap);
	fprintf(stderr, "error: %s\n", g_stvq_encode_error);
}

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

typedef struct EncSlot {
	VqaDecodedFrame fr;
	uint8_t *tiles;
	uint8_t *src64;
} EncSlot;

typedef struct PcmFifo {
	int16_t *p;
	unsigned n;
	unsigned cap;
} PcmFifo;

static void enc_slot_clear(EncSlot *sl)
{
	if (!sl)
		return;
	vqa_decoded_frame_clear(&sl->fr);
	free(sl->tiles);
	free(sl->src64);
	memset(sl, 0, sizeof(*sl));
}

static void pcm_fifo_free(PcmFifo *q)
{
	if (!q)
		return;
	free(q->p);
	memset(q, 0, sizeof(*q));
}

static int pcm_fifo_append(PcmFifo *q, const int16_t *s, size_t n)
{
	unsigned need;

	if (!q || !n)
		return 0;
	if (!s)
		return -1;
	need = q->n + (unsigned)n;
	if (need < q->n)
		return -1;
	if (need > q->cap) {
		unsigned ncap = q->cap ? q->cap * 2u : 16384u;
		int16_t *np;
		while (ncap < need)
			ncap *= 2u;
		np = (int16_t *)realloc(q->p, (size_t)ncap * sizeof(int16_t));
		if (!np)
			return -1;
		q->p = np;
		q->cap = ncap;
	}
	memcpy(q->p + q->n, s, n * sizeof(int16_t));
	q->n = need;
	return 0;
}

static unsigned pcm_fifo_pop(PcmFifo *q, int16_t *dst, unsigned n)
{
	unsigned take;

	if (!q || !n || !q->n)
		return 0;
	take = n < q->n ? n : q->n;
	if (dst)
		memcpy(dst, q->p, take * sizeof(int16_t));
	q->n -= take;
	if (q->n)
		memmove(q->p, q->p + take, (size_t)q->n * sizeof(int16_t));
	return take;
}

/* Picture-tick dest length at 12517 Hz; odd delta rounded up to even (15 fps: 834 or 836). */
static unsigned fps_tick_samples(unsigned fps, unsigned frame_index)
{
	uint64_t t0, t1;
	unsigned n;
	if (!fps)
		fps = 15;
	t0 = ((uint64_t)frame_index * STVQ_SAMPLE_RATE + fps / 2u) / fps;
	t1 = ((uint64_t)(frame_index + 1u) * STVQ_SAMPLE_RATE + fps / 2u) / fps;
	n = (unsigned)(t1 - t0);
	return (n + 1u) & ~1u;
}

static int fifo_push_to_src(StHostSrc *src, PcmFifo *fifo)
{
	int16_t tmp[512];
	unsigned sp, chunk, got;

	sp = st_host_src_in_space(src);
	if (!sp || !fifo->n)
		return 0;
	chunk = fifo->n < sp ? fifo->n : sp;
	if (chunk > 512u)
		chunk = 512u;
	chunk &= ~1u;
	if (!chunk) {
		chunk = fifo->n < sp ? fifo->n : sp;
		if (chunk > 512u)
			chunk = 512u;
	}
	if (!chunk)
		return 0;
	got = pcm_fifo_pop(fifo, tmp, chunk);
	if (got != chunk)
		return -1;
	return st_host_src_push_s16(src, tmp, chunk) ? 0 : -1;
}

static int pull_fps_snd(StHostSrc *src, int *opened, unsigned src_rate, PcmFifo *fifo, signed char *dst,
    unsigned dest_n)
{
	unsigned got = 0;

	if (!dest_n)
		return 0;
	if (!*opened && src_rate && fifo->n) {
		if (!st_host_src_open(src, src_rate))
			return -1;
		*opened = 1;
	}
	if (!*opened) {
		memset(dst, 0, dest_n);
		return 0;
	}
	while (got < dest_n) {
		unsigned g = st_host_src_pull_s8(src, dst + got, dest_n - got);
		unsigned n_before;
		if (g) {
			got += g;
			continue;
		}
		if (!fifo->n) {
			memset(dst + got, 0, dest_n - got);
			break;
		}
		n_before = fifo->n;
		if (fifo_push_to_src(src, fifo) != 0)
			return -1;
		if (fifo->n == n_before) {
			if (fifo->n == 1u) {
				pcm_fifo_pop(fifo, NULL, 1u);
				continue;
			}
			return -1;
		}
	}
	return 0;
}

int stvq_encode(const StvqEncodeOpts *opts)
{
	VqaStream *st = NULL;
	EncSlot *win = NULL;
	unsigned win_n = 0, win_cap = 0;
	StvqSegPalette *segpal = NULL;
	StvqC2P *c2ps = NULL;
	uint8_t **pal_copy = NULL;
	const uint8_t **seg_pal768 = NULL;
	const uint8_t **seg_subset = NULL;
	unsigned seg_ready = 0, seg_cap = 0;
	VqaPalSegment dummy_seg;
	uint8_t *recon[2] = {NULL, NULL};
	uint8_t *recon_work = NULL;
	unsigned *cb_nearest = NULL;
	unsigned *cb_dist = NULL;
	StvqCodebook cb;
	StvqWriter w;
	StvqHeader hdr;
	StHostSrc asrc;
	char out_path[1024];
	unsigned tiles_x, tiles_y, tiles_n;
	unsigned f = 0, s = 0;
	unsigned max_frame = 0;
	const char *outp;
	int rc = -1;
	StvqSelectProf sel_prof;
	uint64_t prof_ns_raster = 0, prof_ns_stvd = 0, prof_ns_frame_misc = 0;
	uint64_t prof_ns_frames = 0;
	unsigned frame_count, width, height, fps, src_rate;
	unsigned lookahead;
	int have_audio_src = 0;
	PcmFifo pcm_fifo;
	int last_seg = -1;

	memset(&cb, 0, sizeof(cb));
	memset(&w, 0, sizeof(w));
	memset(&sel_prof, 0, sizeof(sel_prof));
	memset(&asrc, 0, sizeof(asrc));
	memset(&pcm_fifo, 0, sizeof(pcm_fifo));
	memset(&dummy_seg, 0, sizeof(dummy_seg));

	g_stvq_encode_error[0] = '\0';

	if (!opts || !opts->vqa_path) {
		stvq_encode_set_error("encode requires a VQA path");
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
	if (vqa_stream_open(opts->vqa_path, &st) != 0) {
		stvq_encode_set_error("VQA decode failed (open)");
		return -1;
	}

	frame_count = vqa_stream_frame_count(st);
	width = vqa_stream_width(st);
	height = vqa_stream_height(st);
	fps = vqa_stream_header(st)->fps ? vqa_stream_header(st)->fps : 15;
	src_rate = st_host_normalize_rate(vqa_stream_sample_rate(st));
	lookahead = opts->cb_lookahead;
	win_cap = lookahead + 1u;
	if (win_cap < 2u)
		win_cap = 2u;

	tiles_x = stvq_tiles_x(width);
	tiles_y = stvq_tiles_y(height);
	tiles_n = tiles_x * tiles_y;
	if (tiles_y > 32u) {
		stvq_encode_set_error("tiles_y=%u exceeds STVD mask width (32)", tiles_y);
		goto done;
	}
	if (frame_count > 0xffffu) {
		stvq_encode_set_error("frame count %u exceeds STVQ u16 frames field", frame_count);
		goto done;
	}

	fprintf(stderr, "frames=%u size=%ux%u tiles=%ux%u cb=%u R=%u shortlist=%u*%u random=%u%% "
	                "lookahead=%u cand=f_end gamma=%.3g dct-alpha=%.3g dct-coeffs=%u+%u+%u (feat=%u)\n",
	    frame_count, width, height, tiles_x, tiles_y, opts->cb_size, opts->cb_per_frame, 2u,
	    opts->cb_per_frame, opts->cb_random_pct, opts->cb_lookahead, stvq_metric_gamma(),
	    stvq_metric_dct_alpha(), stvq_metric_dct_coeffs(), stvq_metric_dct_chroma_coeffs(),
	    stvq_metric_dct_chroma_coeffs(), stvq_metric_feat_len());

	if (opts->dry_run) {
		VqaDecodedFrame fr;
		unsigned nseg = 0;
		const VqaPalSegment *segs;
		while (vqa_stream_next(st, &fr) == 1)
			vqa_decoded_frame_clear(&fr);
		segs = vqa_stream_segments(st, &nseg);
		for (s = 0; s < nseg; s++) {
			char pal[768], hist[768], w16p[768];
			if (opts->have_w16_crc && opts->w16_dir) {
				stvq_crc_w16_path(opts->w16_dir, opts->w16_crc, (int)s, w16p, sizeof(w16p));
				fprintf(stderr, "  seg %u frames %d..%d → %s\n", s, segs[s].start_frame,
				    segs[s].end_frame, w16p);
			} else {
				stvq_sidecar_paths(opts->vqa_path, (int)s, pal, hist, w16p, sizeof(pal));
				fprintf(stderr, "  seg %u frames %d..%d → %s / %s / %s\n", s, segs[s].start_frame,
				    segs[s].end_frame, pal, hist, w16p);
			}
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

	win = (EncSlot *)calloc(win_cap, sizeof(*win));
	if (!win) {
		stvq_encode_set_error("out of memory (frame window, %u slots)", win_cap);
		goto done;
	}

	fprintf(stderr, "allocating codebook (%u)...\n", opts->cb_size);
	if (stvq_codebook_alloc(&cb, opts->cb_size) != 0) {
		stvq_encode_set_error("out of memory (codebook, %u entries)", opts->cb_size);
		goto done;
	}

	recon[0] = (uint8_t *)calloc(tiles_n, 32u);
	recon[1] = (uint8_t *)calloc(tiles_n, 32u);
	recon_work = (uint8_t *)calloc(tiles_n, 32u);
	cb_nearest = (unsigned *)malloc((size_t)tiles_n * sizeof(unsigned));
	cb_dist = (unsigned *)malloc((size_t)tiles_n * sizeof(unsigned));
	if (!recon[0] || !recon[1] || !recon_work || !cb_nearest || !cb_dist) {
		stvq_encode_set_error("out of memory (recon/nearest, %u tiles)", tiles_n);
		goto done;
	}

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
	hdr.flags = (vqa_stream_header(st)->flags & 1u) ? 1u : 0u;
	hdr.frames = (uint16_t)frame_count;
	hdr.width = (uint16_t)width;
	hdr.height = (uint16_t)height;
	hdr.block_w = 8;
	hdr.block_h = 8;
	hdr.fps = (uint8_t)fps;
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

	if (opts->progress)
		opts->progress(opts->progress_ctx, "encode", 0, frame_count);

	for (f = 0; f < frame_count; f++) {
		unsigned need = f + lookahead;
		long fr_pos;
		StvqReplace *reps = NULL;
		unsigned nrep = 0;
		unsigned stvd_bytes = 0;
		unsigned char *snd = NULL;
		unsigned snd_n = 0;
		int force_full = (f < 2);
		const uint8_t *recon_n2 = (f >= 2) ? recon[1] : NULL;
		const uint8_t *recon_n1 = (f >= 1) ? recon[0] : NULL;
		int seg;
		int emit_stpl = 0;
		unsigned max_rep;
		uint64_t frame_t0 = enc_ns_now(), t0, t1;
		const uint8_t **win_tiles = NULL;
		const uint8_t **win_src = NULL;
		int *win_seg = NULL;
		unsigned wi;
		unsigned nseg = 0;
		const VqaPalSegment *segs;

		if (need >= frame_count)
			need = frame_count - 1u;
		/* Keep at least one extra decoded frame so sinc has FIR lookahead. */
		if (need < f + 1u && f + 1u < frame_count)
			need = f + 1u;
		if (need >= frame_count)
			need = frame_count - 1u;
		while (f + win_n <= need) {
			EncSlot *sl = &win[win_n];
			int nrc;
			enc_slot_clear(sl);
			nrc = vqa_stream_next(st, &sl->fr);
			if (nrc != 1) {
				stvq_encode_set_error("VQA decode stopped at frame %u/%u (rc=%d)", f, frame_count, nrc);
				goto done;
			}
			sl->tiles = (uint8_t *)malloc((size_t)tiles_n * 32u);
			sl->src64 = (uint8_t *)malloc((size_t)tiles_n * 64u);
			if (!sl->tiles || !sl->src64) {
				stvq_encode_set_error("out of memory (frame tiles) at %u/%u", f, frame_count);
				goto done;
			}
			if (pcm_fifo_append(&pcm_fifo, sl->fr.pcm16, sl->fr.pcm16_count) != 0) {
				stvq_encode_set_error("out of memory (PCM FIFO) at frame %u/%u", f, frame_count);
				goto done;
			}
			free(sl->fr.pcm16);
			sl->fr.pcm16 = NULL;
			sl->fr.pcm16_count = 0;
			win_n++;
		}

		segs = vqa_stream_segments(st, &nseg);
		if (nseg == 0) {
			nseg = 1;
			segs = &dummy_seg;
			dummy_seg.start_frame = 0;
			dummy_seg.end_frame = (int)frame_count - 1;
		}
		while (seg_ready < nseg) {
			unsigned ns;
			StvqSegPalette *npal;
			StvqC2P *nc2p;
			uint8_t **npc;
			const uint8_t **n768, **nsub;
			ns = nseg;
			npal = (StvqSegPalette *)realloc(segpal, ns * sizeof(*npal));
			nc2p = (StvqC2P *)realloc(c2ps, ns * sizeof(*nc2p));
			npc = (uint8_t **)realloc(pal_copy, ns * sizeof(*npc));
			n768 = (const uint8_t **)realloc((void *)seg_pal768, ns * sizeof(*n768));
			nsub = (const uint8_t **)realloc((void *)seg_subset, ns * sizeof(*nsub));
			if (!npal || !nc2p || !npc || !n768 || !nsub) {
				stvq_encode_set_error("out of memory (palette segments, nseg=%u)", ns);
				goto done;
			}
			segpal = npal;
			c2ps = nc2p;
			pal_copy = npc;
			seg_pal768 = n768;
			seg_subset = nsub;
			seg_cap = ns;
			for (; seg_ready < nseg; seg_ready++) {
				int load_rc;
				pal_copy[seg_ready] = (uint8_t *)malloc(VQA_PALETTE_BYTES);
				if (!pal_copy[seg_ready])
					goto done;
				memcpy(pal_copy[seg_ready], segs[seg_ready].pal, VQA_PALETTE_BYTES);
				if (opts->have_w16_crc && opts->w16_dir) {
					load_rc = stvq_load_segment_w16_crc(
					    opts->w16_dir, opts->w16_crc, (int)seg_ready, &segs[seg_ready],
					    &segpal[seg_ready]);
				} else {
					load_rc = stvq_load_segment_w16(
					    opts->vqa_path, (int)seg_ready, &segs[seg_ready], &segpal[seg_ready]);
				}
				if (load_rc != 0) {
					char w16p[768];
					if (opts->have_w16_crc && opts->w16_dir)
						stvq_crc_w16_path(opts->w16_dir, opts->w16_crc, (int)seg_ready, w16p,
						    sizeof(w16p));
					else
						snprintf(w16p, sizeof(w16p), "segment %u sidecar", seg_ready);
					stvq_encode_set_error("missing or bad W16 %s (palette segment %u/%u)", w16p,
					    seg_ready, nseg);
					goto done;
				}
				stvq_c2p_init(&c2ps[seg_ready], &segpal[seg_ready].w16);
				seg_pal768[seg_ready] = pal_copy[seg_ready];
				seg_subset[seg_ready] = segpal[seg_ready].w16.subset;
			}
		}

		seg = win[0].fr.segment;
		{
			uint64_t rt0 = enc_ns_now();
			if (stvq_frame_to_tiles(&c2ps[seg], win[0].fr.pixels, width, height, tiles_x, tiles_y,
			        win[0].tiles, win[0].src64) != 0)
				goto done;
			for (wi = 1; wi < win_n; wi++) {
				int sgi = win[wi].fr.segment;
				if (stvq_frame_to_tiles(&c2ps[sgi], win[wi].fr.pixels, width, height, tiles_x,
				        tiles_y, win[wi].tiles, win[wi].src64) != 0)
					goto done;
			}
			prof_ns_raster += enc_ns_now() - rt0;
		}

		if (f == 0) {
			stvq_metric_set_palette_vga6(seg_pal768[seg], seg_subset[seg]);
			if (write_stpl_chunk(&w, segpal[seg].stpl) != 0)
				goto done;
			last_seg = seg;
		}

		stvq_metric_set_palette_vga6(seg_pal768[seg], seg_subset[seg]);
		stvq_codebook_recompute_feats(&cb);

		if (f > 0 && seg != last_seg) {
			emit_stpl = 1;
			stvq_codebook_on_palette_change(&cb);
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

		max_rep = opts->cb_per_frame;
		if (max_rep > cb.entries)
			max_rep = cb.entries;

		win_tiles = (const uint8_t **)malloc(win_n * sizeof(*win_tiles));
		win_src = (const uint8_t **)malloc(win_n * sizeof(*win_src));
		win_seg = (int *)malloc(win_n * sizeof(*win_seg));
		if (!win_tiles || !win_src || !win_seg)
			goto done;
		for (wi = 0; wi < win_n; wi++) {
			win_tiles[wi] = win[wi].tiles;
			win_src[wi] = win[wi].src64;
			win_seg[wi] = win[wi].fr.segment;
		}

		{
			unsigned sel_n = lookahead + 1u;
			if (sel_n > win_n)
				sel_n = win_n;

		if (max_rep) {
			reps = (StvqReplace *)malloc(max_rep * sizeof(*reps));
			if (!reps)
				goto done;
			nrep = stvq_codebook_select_replaces(&cb, win_tiles, win_src, sel_n, 0, tiles_n, max_rep,
			    opts->cb_random_pct, sel_n ? sel_n - 1u : 0u, recon_n2, recon_n1, win_seg, seg_pal768,
			    seg_subset, reps, &sel_prof, cb_nearest, cb_dist);
			if (write_stcr_chunk(&w, reps, nrep) != 0) {
				free(reps);
				free((void *)win_tiles);
				free((void *)win_src);
				free(win_seg);
				goto done;
			}
			free(reps);
			reps = NULL;
		} else {
			unsigned ti;
			for (ti = 0; ti < tiles_n; ti++)
				cb_nearest[ti] = stvq_codebook_nearest(&cb, win[0].src64 + ti * 64u, &cb_dist[ti]);
		}
		}
		free((void *)win_tiles);
		free((void *)win_src);
		free(win_seg);
		win_tiles = NULL;
		win_src = NULL;
		win_seg = NULL;

		t0 = enc_ns_now();
		if (write_stvd_chunk(&w, &cb, win[0].src64, cb_nearest, cb_dist, recon_n2, recon_n1, recon_work,
		        tiles_x, tiles_y, force_full, &stvd_bytes) != 0)
			goto done;
		t1 = enc_ns_now();
		prof_ns_stvd += t1 - t0;

		memcpy(recon[1], recon[0], (size_t)tiles_n * 32u);
		memcpy(recon[0], recon_work, (size_t)tiles_n * 32u);

		{
			unsigned dest_n = fps_tick_samples(fps, f);
			if (dest_n) {
				snd = (unsigned char *)malloc(dest_n);
				if (!snd) {
					stvq_encode_set_error("out of memory (SND0 %u bytes) at frame %u/%u", dest_n, f,
					    frame_count);
					goto done;
				}
				if (pull_fps_snd(&asrc, &have_audio_src, src_rate, &pcm_fifo, (signed char *)snd,
				        dest_n) != 0) {
					free(snd);
					stvq_encode_set_error("audio pull failed at frame %u/%u (dest_n=%u)", f, frame_count,
					    dest_n);
					goto done;
				}
				snd_n = dest_n;
			}
		}

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
		last_seg = seg;
		enc_slot_clear(&win[0]);
		if (win_n > 1)
			memmove(&win[0], &win[1], (win_n - 1u) * sizeof(win[0]));
		if (win_n)
			win_n--;
		memset(&win[win_n], 0, sizeof(win[0]));

		if (opts->progress)
			opts->progress(opts->progress_ctx, "encode", f + 1u, frame_count);
		else if ((f % 50u) == 0u)
			fprintf(stderr, "  frame %u/%u\n", f, frame_count);
	}

	if (stvq_write_chunk_raw(&w, STVQ_CHUNK_STEN, NULL, 0) != 0)
		goto done;
	if (stvq_write_form_end(&w) != 0)
		goto done;

	{
		unsigned char be[2];
		long sthd_data = 12 + 8;
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
		double nf = frame_count ? (double)frame_count : 1.0;
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
		(void)prof_ns_frame_misc;
		(void)seg_cap;
	}
	rc = 0;
	g_stvq_encode_error[0] = '\0';

done:
	if (rc != 0 && !g_stvq_encode_error[0])
		stvq_encode_set_error("encode aborted (out of memory or I/O) at frame %u/%u", f, frame_count);
	if (w.fp)
		stvq_writer_close(&w);
	stvq_codebook_free(&cb);
	st_host_src_close(&asrc);
	pcm_fifo_free(&pcm_fifo);
	if (win) {
		for (f = 0; f < win_cap; f++)
			enc_slot_clear(&win[f]);
		free(win);
	}
	free(recon[0]);
	free(recon[1]);
	free(recon_work);
	free(cb_nearest);
	free(cb_dist);
	if (pal_copy) {
		for (s = 0; s < seg_ready; s++)
			free(pal_copy[s]);
		free(pal_copy);
	}
	free((void *)seg_pal768);
	free((void *)seg_subset);
	free(c2ps);
	free(segpal);
	vqa_stream_close(st);
	return rc;
}
