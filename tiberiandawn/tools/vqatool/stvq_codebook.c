/*
 * stvq_codebook.c - Per-frame STCR selection (no initial train / STCB).
 */
#include "stvq_codebook.h"
#include "stvq_c2p.h"
#include "stvq_metric.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t stvq_ns_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int stvq_codebook_alloc(StvqCodebook *cb, unsigned entries)
{
	unsigned stride = stvq_metric_feat_len();

	memset(cb, 0, sizeof(*cb));
	cb->entries = entries;
	cb->feat_stride = stride ? stride : 1u;
	cb->tiles = (uint8_t *)calloc(entries, 32u);
	cb->pens = (uint8_t *)calloc(entries, 64u);
	cb->feats = (float *)calloc(entries, cb->feat_stride * sizeof(float));
	cb->use_count = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->last_used = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->tile_epoch = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->pal_epoch = 0;
	if (!cb->tiles || !cb->pens || !cb->feats || !cb->use_count || !cb->last_used || !cb->tile_epoch) {
		stvq_codebook_free(cb);
		return -1;
	}
	return 0;
}

void stvq_codebook_free(StvqCodebook *cb)
{
	free(cb->tiles);
	free(cb->pens);
	free(cb->feats);
	free(cb->use_count);
	free(cb->last_used);
	free(cb->tile_epoch);
	memset(cb, 0, sizeof(*cb));
}

static float *cb_feat(StvqCodebook *cb, unsigned i)
{
	return cb->feats + (size_t)i * cb->feat_stride;
}

static const float *cb_feat_c(const StvqCodebook *cb, unsigned i)
{
	return cb->feats + (size_t)i * cb->feat_stride;
}

static int cb_ensure_feat_stride(StvqCodebook *cb)
{
	unsigned stride = stvq_metric_feat_len();
	float *nf;

	if (!stride)
		stride = 1u;
	if (stride == cb->feat_stride)
		return 0;
	nf = (float *)realloc(cb->feats, (size_t)cb->entries * stride * sizeof(float));
	if (!nf)
		return -1;
	cb->feats = nf;
	cb->feat_stride = stride;
	return 0;
}

static void cb_set_tile(StvqCodebook *cb, unsigned i, const uint8_t tile[32], uint32_t epoch)
{
	memcpy(cb->tiles + i * 32u, tile, 32);
	stvq_unpack_tile_32(tile, cb->pens + i * 64u);
	stvq_metric_feat_from_pens(cb->pens + i * 64u, cb_feat(cb, i));
	cb->tile_epoch[i] = epoch;
}

void stvq_codebook_recompute_feats(StvqCodebook *cb)
{
	unsigned i;
	if (!cb || !cb->feats)
		return;
	if (cb_ensure_feat_stride(cb) != 0)
		return;
	for (i = 0; i < cb->entries; i++)
		stvq_metric_feat_from_pens(cb->pens + i * 64u, cb_feat(cb, i));
}

void stvq_codebook_on_palette_change(StvqCodebook *cb)
{
	if (!cb)
		return;
	cb->pal_epoch++;
}

unsigned stvq_codebook_nearest(const StvqCodebook *cb, const uint8_t src_vga[64], unsigned *out_dist)
{
	float q[STVQ_METRIC_MAX_FEAT_LEN];
	unsigned best = 0, best_d = ~0u, i;
	int have = 0;
	stvq_metric_feat_from_indices(src_vga, q);
	for (i = 0; i < cb->entries; i++) {
		unsigned d;
		if (cb->tile_epoch[i] != cb->pal_epoch)
			continue; /* wrong-palette tiles must not be drawn */
		d = stvq_metric_feat_dist_lim(q, cb_feat_c(cb, i), best_d);
		if (!have || d < best_d) {
			best_d = d;
			best = i;
			have = 1;
			if (d == 0)
				break;
		}
	}
	if (out_dist)
		*out_dist = have ? best_d : ~0u;
	return best;
}

/* Nearest current-palette CB entry to a precomputed feature vector. */
static unsigned nearest_current_feat(const StvqCodebook *cb, const float *qfeat, unsigned *out_dist)
{
	unsigned best = 0, best_d = ~0u, i;
	int have = 0;
	for (i = 0; i < cb->entries; i++) {
		unsigned d;
		if (cb->tile_epoch[i] != cb->pal_epoch)
			continue;
		d = stvq_metric_feat_dist_lim(qfeat, cb_feat_c(cb, i), best_d);
		if (!have || d < best_d) {
			best_d = d;
			best = i;
			have = 1;
			if (d == 0)
				break;
		}
	}
	if (out_dist)
		*out_dist = have ? best_d : ~0u;
	return best;
}

typedef struct Residual {
	unsigned tile;
	unsigned dist;
} Residual;

typedef struct UtilCand {
	unsigned tile;
	long long util;
} UtilCand;

typedef struct WinTile {
	const uint8_t *src; /* 64 VGA indices */
	float *feat;        /* points into win_feats pool */
	const float *cb_feats; /* codebook feat bank for this tile's palette */
	unsigned nearest;
	unsigned dist;
	unsigned second;
	unsigned second_dist;
} WinTile;

static int residual_cmp_desc(const void *a, const void *b)
{
	const Residual *ra = (const Residual *)a;
	const Residual *rb = (const Residual *)b;
	if (ra->dist < rb->dist)
		return 1;
	if (ra->dist > rb->dist)
		return -1;
	if (ra->tile < rb->tile)
		return 1;
	if (ra->tile > rb->tile)
		return -1;
	return 0;
}

static int util_cand_cmp_desc(const void *a, const void *b)
{
	const UtilCand *ua = (const UtilCand *)a;
	const UtilCand *ub = (const UtilCand *)b;
	if (ua->util < ub->util)
		return 1;
	if (ua->util > ub->util)
		return -1;
	if (ua->tile < ub->tile)
		return 1;
	if (ua->tile > ub->tile)
		return -1;
	return 0;
}

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x ? x : 0xA5A5A5A5u;
	return *state;
}

static void nearest_two_feat_bank(const float *cb_feats, unsigned entries, unsigned feat_stride,
    const float *qfeat, unsigned *best_i, unsigned *best_d, unsigned *second_i, unsigned *second_d)
{
	unsigned i;
	*best_i = 0;
	*best_d = ~0u;
	*second_i = 0;
	*second_d = ~0u;
	for (i = 0; i < entries; i++) {
		unsigned d = stvq_metric_feat_dist_lim(qfeat, cb_feats + (size_t)i * feat_stride, *second_d);
		if (d < *best_d) {
			*second_d = *best_d;
			*second_i = *best_i;
			*best_d = d;
			*best_i = i;
		} else if (d < *second_d) {
			*second_d = d;
			*second_i = i;
		}
	}
}

/* Nearest two among tiles stamped with exactly `epoch` (current-palette bank). */
static void nearest_two_epoch(const StvqCodebook *cb, uint32_t epoch, const float *qfeat, unsigned *best_i,
    unsigned *best_d, unsigned *second_i, unsigned *second_d)
{
	unsigned i;
	*best_i = 0;
	*best_d = ~0u;
	*second_i = 0;
	*second_d = ~0u;
	for (i = 0; i < cb->entries; i++) {
		unsigned d;
		if (cb->tile_epoch[i] != epoch)
			continue;
		d = stvq_metric_feat_dist_lim(qfeat, cb_feat_c(cb, i), *second_d);
		if (d < *best_d) {
			*second_d = *best_d;
			*second_i = *best_i;
			*best_d = d;
			*best_i = i;
		} else if (d < *second_d) {
			*second_d = d;
			*second_i = i;
		}
	}
}

static void update_window_after_replace(const StvqCodebook *cb, WinTile *win, unsigned win_n, unsigned victim,
    const unsigned *d_new, int next_epoch, const float *feats_cur)
{
	unsigned i;
	for (i = 0; i < win_n; i++) {
		unsigned dT = d_new[i];
		if (next_epoch && win[i].cb_feats == feats_cur) {
			/* Next-epoch slot is not drawable yet; keep current-palette NN. */
			if (win[i].nearest == victim || win[i].second == victim)
				nearest_two_epoch(cb, cb->pal_epoch, win[i].feat, &win[i].nearest, &win[i].dist,
				    &win[i].second, &win[i].second_dist);
			continue;
		}
		if (win[i].nearest == victim || win[i].second == victim) {
			nearest_two_feat_bank(win[i].cb_feats, cb->entries, cb->feat_stride, win[i].feat,
			    &win[i].nearest, &win[i].dist, &win[i].second, &win[i].second_dist);
			continue;
		}
		if (dT < win[i].dist) {
			win[i].second = win[i].nearest;
			win[i].second_dist = win[i].dist;
			win[i].nearest = victim;
			win[i].dist = dT;
		} else if (dT < win[i].second_dist) {
			win[i].second = victim;
			win[i].second_dist = dT;
		}
	}
}

/*
 * Benefit of adding new_tile. feat_cur = tile under current palette; feat_post =
 * same pens under post-cut palette (NULL if no cut). Each window tile picks the
 * matching feature vector.
 */
static long long add_utility(const WinTile *win, unsigned win_n, const float *feat_cur, const float *feat_post,
    const float *feats_cur, unsigned *d_new, int next_epoch, StvqSelectProf *prof)
{
	unsigned i;
	long long util = 0;
	uint64_t t0 = stvq_ns_now();

	for (i = 0; i < win_n; i++) {
		const float *nf;
		unsigned d;
		if (next_epoch) {
			/* Pre-buffer: only post-cut window tiles can use this slot after STPL. */
			if (win[i].cb_feats == feats_cur) {
				d_new[i] = ~0u;
				continue;
			}
			nf = feat_post ? feat_post : feat_cur;
		} else {
			nf = (win[i].cb_feats == feats_cur || !feat_post) ? feat_cur : feat_post;
		}
		d = stvq_metric_feat_dist(win[i].feat, nf);
		d_new[i] = d;
		if (d < win[i].dist)
			util += (long long)(win[i].dist - d);
	}
	if (prof)
		prof->ns_victim_dnew += stvq_ns_now() - t0;
	return util;
}

/*
 * Eviction cost of slot v: tiles that use v as nearest fall back to second.
 * Ignores whatever would be installed in its place.
 */
static void accumulate_evict_damage(const WinTile *win, unsigned win_n, unsigned long long *damage, unsigned k)
{
	unsigned i;
	memset(damage, 0, (size_t)k * sizeof(*damage));
	for (i = 0; i < win_n; i++) {
		unsigned v = win[i].nearest;
		if (v < k && win[i].second_dist > win[i].dist)
			damage[v] += (unsigned long long)(win[i].second_dist - win[i].dist);
	}
}

/*
 * Prefer lowest-index stale prime (tile_epoch < pal_epoch); else least-damage
 * among current-epoch slots. Next-epoch (pre-buffered) slots are last resort.
 */
static unsigned pick_least_damage_victim(const StvqCodebook *cb, const unsigned long long *damage,
    const uint8_t *slot_taken, unsigned frame_index, StvqSelectProf *prof)
{
	unsigned v, best_v = 0, pass;
	unsigned long long best_d = ~0ull;
	uint32_t best_use = ~0u;
	unsigned best_age = 0;
	int have = 0;
	uint64_t t0 = stvq_ns_now();

	for (v = 0; v < cb->entries; v++) {
		if (slot_taken[v])
			continue;
		if (cb->tile_epoch[v] < cb->pal_epoch) {
			if (prof)
				prof->ns_victim_scan += stvq_ns_now() - t0;
			return v;
		}
	}

	for (pass = 0; pass < 2u; pass++) {
		have = 0;
		best_d = ~0ull;
		best_use = ~0u;
		best_age = 0;
		best_v = 0;
		for (v = 0; v < cb->entries; v++) {
			unsigned long long d;
			uint32_t use;
			unsigned age;
			if (slot_taken[v])
				continue;
			if (pass == 0u && cb->tile_epoch[v] != cb->pal_epoch)
				continue;
			d = damage[v];
			use = cb->use_count[v];
			age = frame_index - cb->last_used[v];
			if (!have || d < best_d ||
			    (d == best_d && (use < best_use || (use == best_use && age > best_age)))) {
				best_d = d;
				best_v = v;
				best_use = use;
				best_age = age;
				have = 1;
			}
		}
		if (have) {
			if (prof)
				prof->ns_victim_scan += stvq_ns_now() - t0;
			return best_v;
		}
	}
	if (prof)
		prof->ns_victim_scan += stvq_ns_now() - t0;
	return 0;
}

static int install_at_victim(StvqCodebook *cb, const uint8_t *tile32, unsigned wi, unsigned victim,
    unsigned frame_index, WinTile *win, unsigned win_n, uint8_t *slot_taken, uint8_t *tile_taken,
    const unsigned *d_new, float *feats_post, const uint8_t *post_pal, const uint8_t *post_subset,
    const uint8_t *cur_pal, const uint8_t *cur_subset, uint32_t epoch, int next_epoch, StvqReplace *out,
    unsigned *n)
{
	if (tile_taken[wi] || slot_taken[victim])
		return 0;
	slot_taken[victim] = 1;
	tile_taken[wi] = 1;
	cb_set_tile(cb, victim, tile32, epoch); /* current-palette feats; epoch may be pal_epoch+1 */
	if (feats_post && post_pal && post_subset) {
		stvq_metric_set_palette_vga6(post_pal, post_subset);
		stvq_metric_feat_from_pens(cb->pens + victim * 64u, feats_post + (size_t)victim * cb->feat_stride);
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	}
	cb->use_count[victim] = 1;
	cb->last_used[victim] = frame_index;
	out[*n].index = (uint16_t)victim;
	memcpy(out[*n].tile, tile32, 32);
	(*n)++;
	update_window_after_replace(cb, win, win_n, victim, d_new, next_epoch, cb->feats);
	return 1;
}

static void tile_feats_cur_post(const uint8_t *tile32, float *feat_cur, float *feat_post, float *feats_post,
    const uint8_t *post_pal, const uint8_t *post_subset, const uint8_t *cur_pal, const uint8_t *cur_subset)
{
	stvq_metric_feat_from_tile32(tile32, feat_cur);
	if (!feats_post || !post_pal || !post_subset) {
		if (feat_post)
			memcpy(feat_post, feat_cur, stvq_metric_feat_len() * sizeof(float));
		return;
	}
	stvq_metric_set_palette_vga6(post_pal, post_subset);
	stvq_metric_feat_from_tile32(tile32, feat_post);
	stvq_metric_set_palette_vga6(cur_pal, cur_subset);
}

/* Random accept: damage-only victim, then d_new for window update. wi indexes win[]. */
static int install_replace(StvqCodebook *cb, const uint8_t *const *frame_tiles, unsigned tiles_n, unsigned wi,
    unsigned frame_index, unsigned cut_at, WinTile *win, unsigned win_n, uint8_t *slot_taken, uint8_t *tile_taken,
    unsigned *d_new_scratch, unsigned long long *damage, float *feats_post, const uint8_t *post_pal,
    const uint8_t *post_subset, const uint8_t *cur_pal, const uint8_t *cur_subset, StvqReplace *out, unsigned *n,
    StvqSelectProf *prof)
{
	unsigned victim, src_frame, tile;
	uint32_t epoch;
	int next_epoch;
	const uint8_t *tile32;
	float feat_cur[STVQ_METRIC_MAX_FEAT_LEN], feat_post[STVQ_METRIC_MAX_FEAT_LEN];
	uint64_t t0;
	if (tile_taken[wi])
		return 0;
	src_frame = frame_index + wi / tiles_n;
	tile = wi % tiles_n;
	next_epoch = (cut_at && src_frame >= cut_at);
	epoch = cb->pal_epoch + (next_epoch ? 1u : 0u);
	tile32 = frame_tiles[src_frame] + tile * 32u;
	t0 = stvq_ns_now();
	accumulate_evict_damage(win, win_n, damage, cb->entries);
	if (prof)
		prof->ns_victim_scan += stvq_ns_now() - t0;
	victim = pick_least_damage_victim(cb, damage, slot_taken, frame_index, prof);
	tile_feats_cur_post(tile32, feat_cur, feat_post, feats_post, post_pal, post_subset, cur_pal, cur_subset);
	(void)add_utility(win, win_n, feat_cur, feats_post ? feat_post : NULL, cb->feats, d_new_scratch, next_epoch,
	    prof);
	return install_at_victim(cb, tile32, wi, victim, frame_index, win, win_n, slot_taken, tile_taken,
	    d_new_scratch, feats_post, post_pal, post_subset, cur_pal, cur_subset, epoch, next_epoch, out, n);
}

/*
 * Cap f_end so [frame_index, f_end] spans at most one segment change.
 * Returns cut frame index (first frame with new segment), or 0 if none.
 */
static unsigned cap_lookahead_one_cut(const int *frame_seg, unsigned frame_index, unsigned *f_end_io)
{
	unsigned f, f_end = *f_end_io;
	int cur;
	unsigned cuts = 0, cut_at = 0;

	if (!frame_seg)
		return 0;
	cur = frame_seg[frame_index];
	for (f = frame_index + 1u; f <= f_end; f++) {
		if (frame_seg[f] == cur)
			continue;
		if (cuts == 0) {
			cuts = 1;
			cut_at = f;
			cur = frame_seg[f];
		} else {
			*f_end_io = f - 1u;
			break;
		}
	}
	return cut_at;
}

unsigned stvq_codebook_select_replaces(StvqCodebook *cb, const uint8_t *const *frame_tiles,
    const uint8_t *const *frame_src, unsigned nframes, unsigned frame_index, unsigned tiles_n,
    unsigned max_replaces, unsigned random_pct, unsigned lookahead, const uint8_t *recon_n2,
    const uint8_t *recon_n1, const int *frame_seg, const uint8_t *const *seg_pal768,
    const uint8_t *const *seg_subset, StvqReplace *out, StvqSelectProf *prof, unsigned *out_nearest,
    unsigned *out_dist)
{
	unsigned n = 0, t, i, f;
	const unsigned thresh = (unsigned)(0.05f * STVQ_METRIC_SCALE + 0.5f);
	Residual *res = NULL;
	UtilCand *ucand = NULL;
	uint8_t *slot_taken = NULL;
	uint8_t *tile_taken = NULL;
	uint8_t *in_shortlist = NULL;
	unsigned *shortlist = NULL;
	unsigned *victims = NULL;
	WinTile *win = NULL;
	float *win_feats = NULL;
	unsigned *d_new = NULL;
	unsigned *eff_dist = NULL;
	unsigned long long *damage = NULL;
	float *feats_post = NULL;
	unsigned win_n = 0, f_end, shortlist_cap, n_sl = 0, n_rand, n_util, cut_at = 0;
	unsigned pool_lo, pool_n, pool_base, feat_stride;
	uint32_t rng;
	const uint8_t *cur_src;
	const uint8_t *cur_pal = NULL;
	const uint8_t *cur_subset = NULL;
	const uint8_t *post_pal = NULL;
	const uint8_t *post_subset = NULL;
	uint64_t t0, t1;

	if (!max_replaces || !tiles_n || !frame_tiles || !frame_src || frame_index >= nframes)
		return 0;
	if (random_pct > 100u)
		random_pct = 100u;

	n_rand = (max_replaces * random_pct) / 100u;
	n_util = max_replaces - n_rand;
	cur_src = frame_src[frame_index];

	for (i = 0; i < cb->entries; i++)
		cb->use_count[i] >>= 1;

	f_end = frame_index + lookahead;
	if (f_end >= nframes)
		f_end = nframes - 1;
	if (frame_seg && seg_pal768 && seg_subset) {
		cut_at = cap_lookahead_one_cut(frame_seg, frame_index, &f_end);
		cur_pal = seg_pal768[frame_seg[frame_index]];
		cur_subset = seg_subset[frame_seg[frame_index]];
		if (cut_at) {
			post_pal = seg_pal768[frame_seg[cut_at]];
			post_subset = seg_subset[frame_seg[cut_at]];
		}
	}
	/* Frame 0: candidates from every window frame; else from f_end only. */
	pool_lo = frame_index ? f_end : frame_index;
	pool_n = (f_end - pool_lo + 1u) * tiles_n;
	pool_base = (pool_lo - frame_index) * tiles_n;
	shortlist_cap = max_replaces * 2u;
	if (shortlist_cap > pool_n)
		shortlist_cap = pool_n;
	win_n = (f_end - frame_index + 1u) * tiles_n;
	feat_stride = cb->feat_stride ? cb->feat_stride : stvq_metric_feat_len();
	if (!feat_stride)
		feat_stride = 1u;

	res = (Residual *)malloc((size_t)pool_n * sizeof(Residual));
	ucand = (UtilCand *)malloc((size_t)shortlist_cap * sizeof(UtilCand));
	slot_taken = (uint8_t *)calloc(cb->entries, 1);
	tile_taken = (uint8_t *)calloc(win_n, 1);
	in_shortlist = (uint8_t *)calloc(win_n, 1);
	shortlist = (unsigned *)malloc((size_t)shortlist_cap * sizeof(unsigned));
	victims = (unsigned *)malloc((size_t)(n_util ? n_util : 1u) * sizeof(unsigned));
	win = (WinTile *)malloc((size_t)win_n * sizeof(WinTile));
	win_feats = (float *)malloc((size_t)win_n * feat_stride * sizeof(float));
	d_new = (unsigned *)malloc((size_t)win_n * sizeof(unsigned));
	eff_dist = (unsigned *)malloc((size_t)win_n * sizeof(unsigned));
	damage = (unsigned long long *)malloc((size_t)cb->entries * sizeof(*damage));
	if (cut_at)
		feats_post = (float *)malloc((size_t)cb->entries * feat_stride * sizeof(float));
	if (!res || !ucand || !slot_taken || !tile_taken || !in_shortlist || !shortlist || !victims || !win ||
	    !win_feats || !d_new || !eff_dist || !damage || (cut_at && !feats_post)) {
		free(res);
		free(ucand);
		free(slot_taken);
		free(tile_taken);
		free(in_shortlist);
		free(shortlist);
		free(victims);
		free(win);
		free(win_feats);
		free(d_new);
		free(eff_dist);
		free(damage);
		free(feats_post);
		return 0;
	}

	if (cut_at) {
		/* CB feats under post-cut palette; leave metric on current palette afterward. */
		stvq_metric_set_palette_vga6(post_pal, post_subset);
		for (i = 0; i < cb->entries; i++)
			stvq_metric_feat_from_pens(cb->pens + i * 64u, feats_post + (size_t)i * feat_stride);
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	}

	t0 = stvq_ns_now();
	win_n = 0;
	for (f = frame_index; f <= f_end; f++) {
		int post = (cut_at && f >= cut_at);
		if (post)
			stvq_metric_set_palette_vga6(post_pal, post_subset);
		else if (cut_at)
			stvq_metric_set_palette_vga6(cur_pal, cur_subset);
		for (t = 0; t < tiles_n; t++) {
			win[win_n].src = frame_src[f] + t * 64u;
			win[win_n].feat = win_feats + (size_t)win_n * feat_stride;
			win[win_n].cb_feats = post ? feats_post : cb->feats;
			stvq_metric_feat_from_indices(win[win_n].src, win[win_n].feat);
			nearest_two_feat_bank(win[win_n].cb_feats, cb->entries, feat_stride, win[win_n].feat,
			    &win[win_n].nearest, &win[win_n].dist, &win[win_n].second, &win[win_n].second_dist);
			win_n++;
		}
	}
	if (cut_at)
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_refresh += t1 - t0;

	/* Stage 1: residual-only shortlist of size 2R from the candidate pool. */
	t0 = stvq_ns_now();
	{
		unsigned n_cand = 0;
		for (f = pool_lo; f <= f_end; f++) {
			unsigned foff = (f - frame_index) * tiles_n;
			for (t = 0; t < tiles_n; t++) {
				unsigned wi = foff + t;
				unsigned dist = win[wi].dist;
				if (f == frame_index && recon_n2 && recon_n1) {
					const uint8_t *n2 = recon_n2 + t * 32u;
					const uint8_t *n1 = recon_n1 + t * 32u;
					if (memcmp(n2, n1, 32) == 0) {
						unsigned dist_n2 = stvq_src_tile_error(cur_src + t * 64u, n2);
						if (dist_n2 < dist)
							dist = dist_n2;
					}
				}
				eff_dist[wi] = dist;
				res[n_cand].tile = wi;
				res[n_cand].dist = dist;
				n_cand++;
			}
		}
		qsort(res, n_cand, sizeof(Residual), residual_cmp_desc);
		for (i = 0; i < n_cand && n_sl < shortlist_cap; i++) {
			t = res[i].tile;
			if (res[i].dist <= thresh)
				break;
			in_shortlist[t] = 1;
			shortlist[n_sl++] = t;
		}
		if (n_sl < shortlist_cap) {
			n_cand = 0;
			for (f = pool_lo; f <= f_end; f++) {
				unsigned foff = (f - frame_index) * tiles_n;
				for (t = 0; t < tiles_n; t++) {
					unsigned wi = foff + t;
					if (in_shortlist[wi])
						continue;
					res[n_cand].tile = wi;
					res[n_cand].dist = eff_dist[wi];
					n_cand++;
				}
			}
			qsort(res, n_cand, sizeof(Residual), residual_cmp_desc);
			for (i = 0; i < n_cand && n_sl < shortlist_cap; i++) {
				t = res[i].tile;
				in_shortlist[t] = 1;
				shortlist[n_sl++] = t;
			}
		}
	}
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_residual += t1 - t0;

	/*
	 * Stage 2 (decoupled):
	 * - Score shortlist by add-utility only (ignore eviction damage).
	 * - Pick victims by eviction damage only (ignore which tile installs).
	 * - Pair top utilities with least-damage victims and install.
	 */
	t0 = stvq_ns_now();
	{
		unsigned n_cand = 0, n_accept, vi;
		uint64_t ts, te;
		float feat_cur[STVQ_METRIC_MAX_FEAT_LEN], feat_post[STVQ_METRIC_MAX_FEAT_LEN];

		ts = stvq_ns_now();
		for (i = 0; i < n_sl; i++) {
			unsigned wi = shortlist[i];
			unsigned src_frame = frame_index + wi / tiles_n;
			unsigned tile = wi % tiles_n;
			int next_epoch = (cut_at && src_frame >= cut_at);
			long long util;
			const uint8_t *tile32 = frame_tiles[src_frame] + tile * 32u;
			tile_feats_cur_post(tile32, feat_cur, feat_post, feats_post, post_pal, post_subset, cur_pal,
			    cur_subset);
			util = add_utility(win, win_n, feat_cur, feats_post ? feat_post : NULL, cb->feats, d_new,
			    next_epoch, prof);
			if (util <= 0)
				continue;
			ucand[n_cand].tile = wi;
			ucand[n_cand].util = util;
			n_cand++;
		}
		te = stvq_ns_now();
		if (prof)
			prof->ns_util_score += te - ts;
		qsort(ucand, n_cand, sizeof(UtilCand), util_cand_cmp_desc);
		n_accept = n_cand;
		if (n_accept > n_util)
			n_accept = n_util;

		ts = stvq_ns_now();
		accumulate_evict_damage(win, win_n, damage, cb->entries);
		if (prof)
			prof->ns_victim_scan += stvq_ns_now() - ts;

		for (vi = 0; vi < n_accept; vi++) {
			victims[vi] = pick_least_damage_victim(cb, damage, slot_taken, frame_index, prof);
			slot_taken[victims[vi]] = 1;
		}

		for (vi = 0; vi < n_accept; vi++)
			slot_taken[victims[vi]] = 0;

		ts = stvq_ns_now();
		for (vi = 0; vi < n_accept; vi++) {
			unsigned wi = ucand[vi].tile;
			unsigned src_frame = frame_index + wi / tiles_n;
			unsigned tile = wi % tiles_n;
			int next_epoch = (cut_at && src_frame >= cut_at);
			uint32_t epoch = cb->pal_epoch + (next_epoch ? 1u : 0u);
			const uint8_t *tile32 = frame_tiles[src_frame] + tile * 32u;
			tile_feats_cur_post(tile32, feat_cur, feat_post, feats_post, post_pal, post_subset, cur_pal,
			    cur_subset);
			(void)add_utility(win, win_n, feat_cur, feats_post ? feat_post : NULL, cb->feats, d_new,
			    next_epoch, prof);
			if (!install_at_victim(cb, tile32, wi, victims[vi], frame_index, win, win_n, slot_taken,
			        tile_taken, d_new, feats_post, post_pal, post_subset, cur_pal, cur_subset, epoch,
			        next_epoch, out, &n))
				break;
		}
		te = stvq_ns_now();
		if (prof)
			prof->ns_util_install += te - ts;
	}
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_utility += t1 - t0;

	/* Stage 3: random tiles from the same candidate pool (damage-only victim). */
	t0 = stvq_ns_now();
	rng = frame_index * 2654435761u + 0x9E3779B9u;
	for (i = 0; i < n_rand && n < max_replaces; i++) {
		unsigned tries;
		for (tries = 0; tries < pool_n; tries++) {
			unsigned wi = pool_base + (xorshift32(&rng) % pool_n);
			if (install_replace(cb, frame_tiles, tiles_n, wi, frame_index, cut_at, win, win_n, slot_taken,
			        tile_taken, d_new, damage, feats_post, post_pal, post_subset, cur_pal, cur_subset, out,
			        &n, prof))
				break;
		}
	}
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_random += t1 - t0;

	for (t = 0; t < tiles_n; t++) {
		unsigned ni, nd;
		/* STVD may only reference current-palette CB entries. */
		ni = nearest_current_feat(cb, win[t].feat, &nd);
		cb->use_count[ni]++;
		cb->last_used[ni] = frame_index;
		if (out_nearest)
			out_nearest[t] = ni;
		if (out_dist)
			out_dist[t] = nd;
	}

	if (cut_at && cur_pal && cur_subset)
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);

	free(res);
	free(ucand);
	free(slot_taken);
	free(tile_taken);
	free(in_shortlist);
	free(shortlist);
	free(victims);
	free(win);
	free(win_feats);
	free(d_new);
	free(eff_dist);
	free(damage);
	free(feats_post);
	return n;
}
