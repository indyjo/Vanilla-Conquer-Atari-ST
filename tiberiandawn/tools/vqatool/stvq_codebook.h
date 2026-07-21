/*
 * stvq_codebook.h - Codebook train + per-frame STCR selection.
 */
#ifndef STVQ_CODEBOOK_H
#define STVQ_CODEBOOK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqCodebook {
	unsigned entries;
	uint8_t *tiles; /* entries * 32 */
	uint8_t *pens;  /* entries * 64 unpacked pens (cache for metric) */
	float *feats;   /* entries * STVQ_METRIC_MAX_COEFFS DCT features */
	uint32_t *use_count;
	uint32_t *last_used;
} StvqCodebook;

int stvq_codebook_alloc(StvqCodebook *cb, unsigned entries);
void stvq_codebook_free(StvqCodebook *cb);
/* Recompute DCT features after palette/metric change. */
void stvq_codebook_recompute_feats(StvqCodebook *cb);

/*
 * Train from paired dithered tiles (32B) and original VGA src tiles (64B).
 * Codebook stores dithered tiles; assignment uses YUV(src) vs pens.
 */
int stvq_codebook_train(StvqCodebook *cb, const uint8_t *const *frame_tiles,
    const uint8_t *const *frame_src, unsigned nframes, unsigned tiles_per_frame, unsigned sample_stride);

/* Nearest CB entry to original VGA src tile (64 indices). */
unsigned stvq_codebook_nearest(const StvqCodebook *cb, const uint8_t src_vga[64], unsigned *out_dist);

typedef struct StvqReplace {
	uint16_t index;
	uint8_t tile[32];
} StvqReplace;

/* Optional timing buckets filled by stvq_codebook_select_replaces (nanoseconds). */
typedef struct StvqSelectProf {
	uint64_t ns_refresh;      /* window nearest_two assign */
	uint64_t ns_residual;     /* stage-1 residual shortlist */
	uint64_t ns_utility;      /* stage-2 total (score + install) */
	uint64_t ns_util_score;   /* shortlist add-utility (ignore eviction) */
	uint64_t ns_util_install; /* pair + install */
	uint64_t ns_victim_dnew;  /* d_new vs window (for add-util / install update) */
	uint64_t ns_victim_scan;  /* eviction-damage accumulate / pick */
	uint64_t ns_random;       /* stage-3 direct random installs */
} StvqSelectProf;

/*
 * Two-stage STCR selection (+ direct random), accept/evict decoupled:
 * 1) Residual-only shortlist of min(2*R, tiles_n); residual considers stay-as-is.
 * 2) Score shortlist by add-utility only (ignore eviction); pick victims by
 *    eviction-damage only (ignore install); pair top utilities with least-damage
 *    victims.
 * 3) Accept random_pct% random current-frame tiles (damage-only victim).
 * Error = weighted Y-DCT feature L2 (see stvq_metric).
 * frame_tiles = dithered 32B (installed into CB); frame_src = VGA 64B (metric).
 * recon_n2 / recon_n1 may be NULL (no stay-as-is).
 * prof may be NULL.
 * out_nearest / out_dist may be NULL; if non-NULL, filled with post-STCR
 * nearest CB index and YUV error for each current-frame tile (tiles_n).
 */
unsigned stvq_codebook_select_replaces(StvqCodebook *cb, const uint8_t *const *frame_tiles,
    const uint8_t *const *frame_src, unsigned nframes, unsigned frame_index, unsigned tiles_n,
    unsigned max_replaces, unsigned random_pct, unsigned lookahead, const uint8_t *recon_n2,
    const uint8_t *recon_n1, StvqReplace *out, StvqSelectProf *prof, unsigned *out_nearest,
    unsigned *out_dist);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_CODEBOOK_H */
