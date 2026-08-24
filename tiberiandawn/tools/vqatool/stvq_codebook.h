/*
 * stvq_codebook.h - Per-frame STCR selection (empty CB filled via replaces).
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
	unsigned feat_stride; /* == stvq_metric_feat_len() at alloc / last resize */
	uint8_t *tiles; /* entries * 32 */
	uint8_t *pens;  /* entries * 64 unpacked pens (cache for metric) */
	float *feats;   /* entries * feat_stride DCT features */
	uint32_t *use_count;
	uint32_t *last_used;
	/*
	 * Palette epoch: a tile is a stale prime iff tile_epoch[i] < pal_epoch
	 * (installed under a previous STPL/W16). stvq_codebook_on_palette_change
	 * bumps pal_epoch so prior-palette entries become prime.
	 * Pre-buffered post-cut installs use pal_epoch+1 so they become valid
	 * after the bump and are not treated as primes before the cut.
	 */
	uint32_t pal_epoch;
	uint32_t *tile_epoch;
} StvqCodebook;

int stvq_codebook_alloc(StvqCodebook *cb, unsigned entries);
void stvq_codebook_free(StvqCodebook *cb);
/* Recompute DCT features after palette/metric change. */
void stvq_codebook_recompute_feats(StvqCodebook *cb);
/* Bump pal_epoch so prior-palette tiles become stale primes. Next-epoch
 * pre-buffered tiles (pal_epoch+1 before the bump) become current. */
void stvq_codebook_on_palette_change(StvqCodebook *cb);

/* Nearest current-palette CB entry to original VGA src tile (64 indices).
 * Entries from a prior palette epoch are ignored. */
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
 * Candidate tiles: farthest lookahead frame (f_end). On encode frame 0, every
 * frame in the window. Stay-as-is residual applies only to current-frame
 * candidates. Utility and eviction still score the full [frame, f_end] window.
 * 1) Residual-only shortlist of min(2*R, pool); residual considers stay-as-is
 *    on the current frame only.
 * 2) Score shortlist by add-utility only (ignore eviction); pick victims by
 *    eviction-damage only (ignore install), except: after a palette change,
 *    entries with tile_epoch < pal_epoch are prime eviction candidates —
 *    always prefer a free stale prime (lowest index) over damage-based victims.
 *    Next-epoch (pre-buffered) slots are evicted only if no current-epoch slot
 *    remains. Pair top utilities with those victims.
 * 3) Accept random_pct% random tiles from the same candidate pool (same victim
 *    policy).
 * Error = weighted YUV-DCT feature L2 (see stvq_metric).
 * frame_tiles = dithered 32B (installed into CB); frame_src = VGA 64B (metric).
 * recon_n2 / recon_n1 may be NULL (no stay-as-is).
 * prof may be NULL.
 * out_nearest / out_dist may be NULL; if non-NULL, filled with post-STCR
 * nearest CB index and YUV error for each current-frame tile (tiles_n).
 *
 * Lookahead vs palette: frame_seg[f] = segment id for frame f. If non-NULL with
 * seg_pal768 / seg_subset, the lookahead window is capped to at most one segment
 * change; tiles before the cut use the current palette (already set on entry),
 * tiles at/after the cut use that next segment's palette for source + CB feats.
 * Post-cut candidate installs are stamped pal_epoch+1 (not drawn until STPL).
 */
unsigned stvq_codebook_select_replaces(StvqCodebook *cb, const uint8_t *const *frame_tiles,
    const uint8_t *const *frame_src, unsigned nframes, unsigned frame_index, unsigned tiles_n,
    unsigned max_replaces, unsigned random_pct, unsigned lookahead, const uint8_t *recon_n2,
    const uint8_t *recon_n1, const int *frame_seg, const uint8_t *const *seg_pal768,
    const uint8_t *const *seg_subset, StvqReplace *out, StvqSelectProf *prof, unsigned *out_nearest,
    unsigned *out_dist);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_CODEBOOK_H */
