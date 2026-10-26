/*
 * stvq_codebook.h - Per-frame STCR selection (empty CB filled via replaces).
 */
#ifndef STVQ_CODEBOOK_H
#define STVQ_CODEBOOK_H

#include "stvq_trace.h"

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
	uint32_t *color_hash; /* FNV-1a of the 64 DCT color indices; equal → distance 0 */
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

/* Nearest current-palette CB entry to a dithered 32-byte tile.
 * Entries from a prior palette epoch are ignored. */
unsigned stvq_codebook_nearest(const StvqCodebook *cb, const uint8_t tile32[32], unsigned *out_dist);

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
	uint64_t ns_random;       /* random blocks drawn into the shortlist */
	/* Per call, not accumulated. util_events may be NULL. */
	unsigned nscore;
	StviUtilEvent *util_events;
	unsigned util_event_cap;
	unsigned n_util_events;
} StvqSelectProf;

/*
 * Two-stage STCR selection, accept/evict decoupled:
 * Candidate tiles: farthest lookahead frame (f_end). On encode frame 0, every
 * frame in the window. Stay-as-is residual applies only to current-frame
 * candidates. Utility and eviction still score the full [frame, f_end] window.
 * A block's residual is the distance to the nearest slot stamped with the
 * epoch that will draw it: pal_epoch, or pal_epoch+1 past a palette cut.
 * Slots from any other epoch are ignored. An epoch that has no slot yet
 * receives one flat pen-0 tile before that search; the install consumes one
 * replace, and the remaining replaces follow the steps below. That pen-0
 * slot can be evicted on a later frame.
 * 1) Shortlist from the candidate pool. By default that is the min(2*R, pool)
 *    worst residuals (stay-as-is on the current frame only). shortlist_all
 *    instead admits every pool block, so the accepts below are chosen only by
 *    add-utility. Then draw up to random_pct% of R extra pool blocks that are
 *    not already shortlisted and whose color hash is not already stored for
 *    that palette epoch. Either way the shortlist keeps one copy of each
 *    color hash. A hash of 0 is a vacant slot (calloc), not a stored tile.
 * 2) Score the shortlist by add-utility only (ignore eviction). A candidate
 *    only helps blocks that will be allowed to draw it. Minoux lazy greedy
 *    then installs the best marginal tile: stored utilities are upper bounds,
 *    and only the current best is recomputed. If it is still the best, it is
 *    installed. An install that raises some block's distance invalidates the
 *    bounds, and the shortlist is scored again. A random shortlist entry
 *    installs only when its recomputed utility is the best and still positive.
 *    Each install picks a victim by eviction damage only
 *    (ignore which tile installs), except: after a palette change, entries
 *    with tile_epoch < pal_epoch are prime eviction candidates — always prefer
 *    a free stale prime (lowest index) over damage-based victims. Next-epoch
 *    (pre-buffered) slots are evicted only if no current-epoch slot remains.
 * Error = weighted YUV-DCT of the dithered 16-color tile (see stvq_metric).
 * frame_tiles = dithered 32B, both installed into the codebook and scored.
 * recon_n2 / recon_n1 may be NULL (no stay-as-is).
 * prof may be NULL.
 * out_nearest / out_dist may be NULL; if non-NULL, filled with post-STCR
 * nearest CB index and YUV error for each current-frame tile (tiles_n).
 * out_win_dist / out_win_pool may be NULL. When set, out_win_dist has
 * nframes * tiles_n entries and out_win_pool has nframes bytes. They record
 * the candidate residual after a pen-0 seed and before the utility installs
 * (effective nearest distance, stay-as-is applied on the current frame only).
 * ~0u and pool 0 mean that frame was
 * outside the candidate pool (past a palette-cut cap).
 *
 * Lookahead vs palette: frame_seg[f] = segment id for frame f. If non-NULL with
 * seg_pal768 / seg_subset, the lookahead window is capped to at most one segment
 * change; tiles before the cut use the current palette (already set on entry),
 * tiles at/after the cut use that next segment's palette for source + CB feats.
 * Post-cut candidate installs are stamped pal_epoch+1 (not drawn until STPL).
 */
/*
 * frame_index is the current frame's slot in frame_tiles. now is the movie
 * frame number written to last_used. Equal damage and equal use_count prefer
 * the oldest last_used.
 */
unsigned stvq_codebook_select_replaces(StvqCodebook *cb, const uint8_t *const *frame_tiles,
    unsigned nframes, unsigned frame_index, unsigned now, unsigned tiles_n,
    unsigned max_replaces, unsigned random_pct, unsigned lookahead, const uint8_t *recon_n2,
    const uint8_t *recon_n1, const int *frame_seg, const uint8_t *const *seg_pal768,
    const uint8_t *const *seg_subset, StvqReplace *out, StvqSelectProf *prof, unsigned *out_nearest,
    unsigned *out_dist, unsigned *out_win_dist, uint8_t *out_win_pool, int shortlist_all);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_CODEBOOK_H */
