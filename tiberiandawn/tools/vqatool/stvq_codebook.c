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
	cb->color_hash = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->use_count = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->last_used = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->tile_epoch = (uint32_t *)calloc(entries, sizeof(uint32_t));
	cb->pal_epoch = 0;
	if (!cb->tiles || !cb->pens || !cb->feats || !cb->color_hash || !cb->use_count || !cb->last_used ||
	    !cb->tile_epoch) {
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
	free(cb->color_hash);
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
	cb->color_hash[i] = stvq_metric_color_hash_pens(cb->pens + i * 64u);
	cb->tile_epoch[i] = epoch;
}

void stvq_codebook_recompute_feats(StvqCodebook *cb)
{
	unsigned i;
	if (!cb || !cb->feats)
		return;
	if (cb_ensure_feat_stride(cb) != 0)
		return;
	for (i = 0; i < cb->entries; i++) {
		stvq_metric_feat_from_pens(cb->pens + i * 64u, cb_feat(cb, i));
		cb->color_hash[i] = stvq_metric_color_hash_pens(cb->pens + i * 64u);
	}
}

void stvq_codebook_on_palette_change(StvqCodebook *cb)
{
	if (!cb)
		return;
	cb->pal_epoch++;
}

unsigned stvq_codebook_nearest(const StvqCodebook *cb, const uint8_t tile32[32], unsigned *out_dist)
{
	float q[STVQ_METRIC_MAX_FEAT_LEN];
	unsigned best = 0, best_d = ~0u, i;
	int have = 0;
	uint32_t qh;
	stvq_metric_feat_from_tile32(tile32, q);
	qh = stvq_metric_color_hash_tile32(tile32);
	for (i = 0; i < cb->entries; i++) {
		unsigned d;
		if (cb->tile_epoch[i] != cb->pal_epoch)
			continue; /* wrong-palette tiles must not be drawn */
		d = stvq_metric_feat_dist_hash_lim(q, qh, cb_feat_c(cb, i), cb->color_hash[i], best_d);
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
static unsigned nearest_current_feat(const StvqCodebook *cb, const float *qfeat, uint32_t qhash, unsigned *out_dist)
{
	unsigned best = 0, best_d = ~0u, i;
	int have = 0;
	for (i = 0; i < cb->entries; i++) {
		unsigned d;
		if (cb->tile_epoch[i] != cb->pal_epoch)
			continue;
		d = stvq_metric_feat_dist_hash_lim(qfeat, qhash, cb_feat_c(cb, i), cb->color_hash[i], best_d);
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
	const uint8_t *tile; /* dithered 32B tile, the metric reference */
	float *feat;         /* points into win_feats pool */
	uint32_t color_hash; /* hash of the colors feat was built from */
	const float *cb_feats; /* codebook feat bank for this tile's palette */
	const uint32_t *cb_hash; /* color hashes parallel to cb_feats */
	uint32_t legal_epoch; /* only slots stamped with this epoch may cover the tile */
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

/* Nearest two slots stamped with `epoch`. No such slot → index `entries`, dist ~0u. */
static void nearest_two_epoch_bank(const float *cb_feats, const uint32_t *cb_hash, const uint32_t *tile_epoch,
    uint32_t epoch, unsigned entries, unsigned feat_stride, const float *qfeat, uint32_t qhash,
    unsigned *best_i, unsigned *best_d, unsigned *second_i, unsigned *second_d)
{
	unsigned i;
	int have = 0;

	*best_i = entries;
	*best_d = ~0u;
	*second_i = entries;
	*second_d = ~0u;
	for (i = 0; i < entries; i++) {
		unsigned d;
		if (tile_epoch[i] != epoch)
			continue;
		d = stvq_metric_feat_dist_hash_lim(qfeat, qhash, cb_feats + (size_t)i * feat_stride, cb_hash[i],
		    *second_d);
		if (!have || d < *best_d) {
			*second_d = *best_d;
			*second_i = *best_i;
			*best_d = d;
			*best_i = i;
			have = 1;
		} else if (d < *second_d) {
			*second_d = d;
			*second_i = i;
		}
	}
}

static void refresh_win_nearest(const StvqCodebook *cb, WinTile *w)
{
	nearest_two_epoch_bank(w->cb_feats, w->cb_hash, cb->tile_epoch, w->legal_epoch, cb->entries,
	    cb->feat_stride, w->feat, w->color_hash, &w->nearest, &w->dist, &w->second, &w->second_dist);
}

static void update_window_after_replace(const StvqCodebook *cb, WinTile *win, unsigned win_n, unsigned victim,
    const unsigned *d_new, int next_epoch)
{
	unsigned i;
	for (i = 0; i < win_n; i++) {
		unsigned dT = d_new[i];
		uint32_t installed = cb->pal_epoch + (next_epoch ? 1u : 0u);
		if (win[i].legal_epoch != installed) {
			/* This slot is not drawable for the window tile. */
			if (win[i].nearest == victim || win[i].second == victim)
				refresh_win_nearest(cb, &win[i]);
			continue;
		}
		if (win[i].nearest == victim || win[i].second == victim) {
			refresh_win_nearest(cb, &win[i]);
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
    uint32_t hash_cur, uint32_t hash_post, const float *feats_cur, unsigned *d_new, int next_epoch,
    StvqSelectProf *prof)
{
	unsigned i;
	long long util = 0;
	uint64_t t0 = stvq_ns_now();

	for (i = 0; i < win_n; i++) {
		const float *nf;
		uint32_t nh;
		unsigned d;
		if (next_epoch) {
			/* Pre-buffer: only post-cut window tiles can use this slot after STPL. */
			if (win[i].cb_feats == feats_cur) {
				d_new[i] = ~0u;
				continue;
			}
			nf = feat_post ? feat_post : feat_cur;
			nh = hash_post;
		} else if (feat_post && win[i].cb_feats != feats_cur) {
			/* A current-epoch slot is not drawable past the palette cut. */
			d_new[i] = ~0u;
			continue;
		} else {
			nf = feat_cur;
			nh = hash_cur;
		}
		d = stvq_metric_feat_dist_hash(win[i].feat, win[i].color_hash, nf, nh);
		d_new[i] = d;
		if (d < win[i].dist)
			util += (long long)(win[i].dist - d);
	}
	if (prof)
		prof->ns_victim_dnew += stvq_ns_now() - t0;
	return util;
}

static void note_util(StvqSelectProf *prof, unsigned pass, unsigned cb_index, unsigned wi, long long predicted,
    long long actual)
{
	StviUtilEvent *e;
	if (!prof || !prof->util_events || prof->n_util_events >= prof->util_event_cap || pass > 0xffffu ||
	    cb_index > 0xffffu)
		return;
	e = &prof->util_events[prof->n_util_events++];
	e->pass = (uint16_t)pass;
	e->cb_index = (uint16_t)cb_index;
	e->wi = wi;
	e->predicted = predicted;
	e->actual = actual;
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
    const uint8_t *slot_taken, unsigned now, StvqSelectProf *prof)
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
			age = now - cb->last_used[v];
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
    unsigned now, WinTile *win, unsigned win_n, uint8_t *slot_taken, uint8_t *tile_taken,
    const unsigned *d_new, float *feats_post, uint32_t *hash_post, const uint8_t *post_pal,
    const uint8_t *post_subset, const uint8_t *cur_pal, const uint8_t *cur_subset, uint32_t epoch, int next_epoch,
    StvqReplace *out, unsigned *n)
{
	if (tile_taken[wi] || slot_taken[victim])
		return 0;
	slot_taken[victim] = 1;
	tile_taken[wi] = 1;
	cb_set_tile(cb, victim, tile32, epoch); /* current-palette feats; epoch may be pal_epoch+1 */
	if (feats_post && post_pal && post_subset) {
		stvq_metric_set_palette_vga6(post_pal, post_subset);
		stvq_metric_feat_from_pens(cb->pens + victim * 64u, feats_post + (size_t)victim * cb->feat_stride);
		if (hash_post)
			hash_post[victim] = stvq_metric_color_hash_pens(cb->pens + victim * 64u);
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	}
	cb->use_count[victim] = 1;
	cb->last_used[victim] = now;
	out[*n].index = (uint16_t)victim;
	memcpy(out[*n].tile, tile32, 32);
	(*n)++;
	update_window_after_replace(cb, win, win_n, victim, d_new, next_epoch);
	return 1;
}

static void tile_feats_cur_post(const uint8_t *tile32, float *feat_cur, float *feat_post, uint32_t *hash_cur,
    uint32_t *hash_post, float *feats_post, const uint8_t *post_pal, const uint8_t *post_subset,
    const uint8_t *cur_pal, const uint8_t *cur_subset)
{
	stvq_metric_feat_from_tile32(tile32, feat_cur);
	*hash_cur = stvq_metric_color_hash_tile32(tile32);
	if (!feats_post || !post_pal || !post_subset) {
		if (feat_post)
			memcpy(feat_post, feat_cur, stvq_metric_feat_len() * sizeof(float));
		*hash_post = *hash_cur;
		return;
	}
	stvq_metric_set_palette_vga6(post_pal, post_subset);
	stvq_metric_feat_from_tile32(tile32, feat_post);
	*hash_post = stvq_metric_color_hash_tile32(tile32);
	stvq_metric_set_palette_vga6(cur_pal, cur_subset);
}

/* True if the shortlist already holds a window tile with this color hash. */
static int shortlist_has_hash(const WinTile *win, const unsigned *shortlist, unsigned n_sl, uint32_t h)
{
	unsigned i;

	for (i = 0; i < n_sl; i++) {
		if (win[shortlist[i]].color_hash == h)
			return 1;
	}
	return 0;
}

/*
 * True if this epoch already stores h. Hash 0 is vacant: cb->color_hash is
 * calloc'd, and cb_set_tile writes a real FNV-1a. Empty hash_post entries are
 * the hash of blank pens, so only slots whose tile_epoch matches are compared.
 */
static int epoch_has_hash(const StvqCodebook *cb, const uint32_t *hash_post, uint32_t epoch, uint32_t h)
{
	unsigned i;

	if (h == 0)
		return 0;
	for (i = 0; i < cb->entries; i++) {
		uint32_t slot_h;
		if (cb->tile_epoch[i] != epoch)
			continue;
		slot_h = (hash_post && epoch != cb->pal_epoch) ? hash_post[i] : cb->color_hash[i];
		if (slot_h != 0 && slot_h == h)
			return 1;
	}
	return 0;
}

static int epoch_occupied(const StvqCodebook *cb, uint32_t epoch)
{
	unsigned i;

	for (i = 0; i < cb->entries; i++) {
		if (cb->tile_epoch[i] == epoch)
			return 1;
	}
	return 0;
}

/* Lowest-index stale slot, else current epoch, else anything not already taken. */
static unsigned pick_seed_slot(const StvqCodebook *cb, const uint8_t *slot_taken)
{
	unsigned v, pass;

	for (pass = 0; pass < 3u; pass++) {
		for (v = 0; v < cb->entries; v++) {
			if (slot_taken[v])
				continue;
			if (pass == 0u && cb->tile_epoch[v] < cb->pal_epoch)
				return v;
			if (pass == 1u && cb->tile_epoch[v] == cb->pal_epoch)
				return v;
			if (pass == 2u)
				return v;
		}
	}
	return 0;
}

/*
 * One flat pen-0 tile for an epoch that has no slot yet. Consumes one replace.
 * The metric is left on the current palette.
 */
static int seed_color0(StvqCodebook *cb, uint32_t epoch, unsigned now, uint8_t *slot_taken, float *feats_post,
    uint32_t *hash_post, const uint8_t *post_pal, const uint8_t *post_subset, const uint8_t *cur_pal,
    const uint8_t *cur_subset, StvqReplace *out, unsigned *n, unsigned *quota)
{
	uint8_t tile[32];
	unsigned victim;

	if (*quota == 0 || epoch_occupied(cb, epoch))
		return 0;
	memset(tile, 0, sizeof(tile));
	victim = pick_seed_slot(cb, slot_taken);
	slot_taken[victim] = 1;
	cb_set_tile(cb, victim, tile, epoch);
	if (feats_post && post_pal && post_subset) {
		stvq_metric_set_palette_vga6(post_pal, post_subset);
		stvq_metric_feat_from_pens(cb->pens + victim * 64u, feats_post + (size_t)victim * cb->feat_stride);
		if (hash_post)
			hash_post[victim] = stvq_metric_color_hash_pens(cb->pens + victim * 64u);
		if (cur_pal && cur_subset)
			stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	}
	cb->use_count[victim] = 1;
	cb->last_used[victim] = now;
	out[*n].index = (uint16_t)victim;
	memcpy(out[*n].tile, tile, 32);
	(*n)++;
	(*quota)--;
	return 1;
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
    unsigned nframes, unsigned frame_index, unsigned now, unsigned tiles_n,
    unsigned max_replaces, unsigned random_pct, unsigned lookahead, const uint8_t *recon_n2,
    const uint8_t *recon_n1, const int *frame_seg, const uint8_t *const *seg_pal768,
    const uint8_t *const *seg_subset, StvqReplace *out, StvqSelectProf *prof, unsigned *out_nearest,
    unsigned *out_dist, unsigned *out_win_dist, uint8_t *out_win_pool, int shortlist_all)
{
	unsigned n = 0, t, i, f;
	const unsigned thresh = (unsigned)(0.05f * STVQ_METRIC_SCALE + 0.5f);
	Residual *res = NULL;
	UtilCand *ucand = NULL;
	uint8_t *slot_taken = NULL;
	uint8_t *tile_taken = NULL;
	uint8_t *in_shortlist = NULL;
	unsigned *shortlist = NULL;
	WinTile *win = NULL;
	float *win_feats = NULL;
	unsigned *d_new = NULL;
	unsigned *eff_dist = NULL;
	unsigned long long *damage = NULL;
	float *feats_post = NULL;
	uint32_t *hash_post = NULL;
	unsigned win_n = 0, f_end, shortlist_cap, residual_cap, n_sl = 0, n_rand, n_util, cut_at = 0;
	unsigned pool_lo, pool_n, pool_base, feat_stride;
	uint32_t rng;
	const uint8_t *cur_pal = NULL;
	const uint8_t *cur_subset = NULL;
	const uint8_t *post_pal = NULL;
	const uint8_t *post_subset = NULL;
	uint64_t t0, t1;

	if (!max_replaces || !tiles_n || !frame_tiles || frame_index >= nframes)
		return 0;
	if (prof) {
		prof->nscore = 0;
		prof->n_util_events = 0;
	}
	if (random_pct > 100u)
		random_pct = 100u;

	/* Random tiles are shortlist entries. Every replace is a utility accept. */
	n_rand = (max_replaces * random_pct) / 100u;
	n_util = max_replaces;

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
	/* Residual shortlist stays 2R (or the whole pool). Random draws use the extra room. */
	residual_cap = shortlist_all ? pool_n : max_replaces * 2u;
	if (residual_cap > pool_n)
		residual_cap = pool_n;
	shortlist_cap = residual_cap;
	if (!shortlist_all) {
		shortlist_cap += n_rand;
		if (shortlist_cap > pool_n)
			shortlist_cap = pool_n;
	}
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
	win = (WinTile *)malloc((size_t)win_n * sizeof(WinTile));
	win_feats = (float *)malloc((size_t)win_n * feat_stride * sizeof(float));
	d_new = (unsigned *)malloc((size_t)win_n * sizeof(unsigned));
	eff_dist = (unsigned *)malloc((size_t)win_n * sizeof(unsigned));
	damage = (unsigned long long *)malloc((size_t)cb->entries * sizeof(*damage));
	if (cut_at) {
		feats_post = (float *)malloc((size_t)cb->entries * feat_stride * sizeof(float));
		hash_post = (uint32_t *)malloc((size_t)cb->entries * sizeof(uint32_t));
	}
	if (!res || !ucand || !slot_taken || !tile_taken || !in_shortlist || !shortlist || !win ||
	    !win_feats || !d_new || !eff_dist || !damage || (cut_at && (!feats_post || !hash_post))) {
		free(res);
		free(ucand);
		free(slot_taken);
		free(tile_taken);
		free(in_shortlist);
		free(shortlist);
		free(win);
		free(win_feats);
		free(d_new);
		free(eff_dist);
		free(damage);
		free(feats_post);
		free(hash_post);
		return 0;
	}

	if (cut_at) {
		/* CB feats under post-cut palette; leave metric on current palette afterward. */
		stvq_metric_set_palette_vga6(post_pal, post_subset);
		for (i = 0; i < cb->entries; i++) {
			stvq_metric_feat_from_pens(cb->pens + i * 64u, feats_post + (size_t)i * feat_stride);
			hash_post[i] = stvq_metric_color_hash_pens(cb->pens + i * 64u);
		}
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	}

	/* An epoch with no drawable slot starts from one flat pen-0 tile. */
	seed_color0(cb, cb->pal_epoch, now, slot_taken, feats_post, hash_post, post_pal, post_subset, cur_pal,
	    cur_subset, out, &n, &n_util);
	if (cut_at)
		seed_color0(cb, cb->pal_epoch + 1u, now, slot_taken, feats_post, hash_post, post_pal, post_subset,
		    cur_pal, cur_subset, out, &n, &n_util);

	t0 = stvq_ns_now();
	win_n = 0;
	for (f = frame_index; f <= f_end; f++) {
		int post = (cut_at && f >= cut_at);
		if (post)
			stvq_metric_set_palette_vga6(post_pal, post_subset);
		else if (cut_at)
			stvq_metric_set_palette_vga6(cur_pal, cur_subset);
		for (t = 0; t < tiles_n; t++) {
			win[win_n].tile = frame_tiles[f] + t * 32u;
			win[win_n].feat = win_feats + (size_t)win_n * feat_stride;
			win[win_n].cb_feats = post ? feats_post : cb->feats;
			win[win_n].cb_hash = post ? hash_post : cb->color_hash;
			win[win_n].legal_epoch = cb->pal_epoch + (post ? 1u : 0u);
			stvq_metric_feat_from_tile32(win[win_n].tile, win[win_n].feat);
			win[win_n].color_hash = stvq_metric_color_hash_tile32(win[win_n].tile);
			refresh_win_nearest(cb, &win[win_n]);
			win_n++;
		}
	}
	if (cut_at)
		stvq_metric_set_palette_vga6(cur_pal, cur_subset);
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_refresh += t1 - t0;

	/* Stage 1: shortlist from the candidate pool. Residual rank, or every block. */
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
						unsigned dist_n2 = stvq_tile_error(win[wi].tile, n2);
						if (dist_n2 < dist)
							dist = dist_n2;
					}
				}
				eff_dist[wi] = dist;
				if (shortlist_all) {
					if (shortlist_has_hash(win, shortlist, n_sl, win[wi].color_hash)) {
						in_shortlist[wi] = 1;
						continue;
					}
					in_shortlist[wi] = 1;
					shortlist[n_sl++] = wi;
				} else {
					res[n_cand].tile = wi;
					res[n_cand].dist = dist;
					n_cand++;
				}
			}
		}
		if (!shortlist_all) {
			qsort(res, n_cand, sizeof(Residual), residual_cmp_desc);
			for (i = 0; i < n_cand && n_sl < residual_cap; i++) {
				t = res[i].tile;
				if (res[i].dist <= thresh)
					break;
				if (shortlist_has_hash(win, shortlist, n_sl, win[t].color_hash)) {
					in_shortlist[t] = 1;
					continue;
				}
				in_shortlist[t] = 1;
				shortlist[n_sl++] = t;
			}
			if (n_sl < residual_cap) {
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
				for (i = 0; i < n_cand && n_sl < residual_cap; i++) {
					t = res[i].tile;
					if (shortlist_has_hash(win, shortlist, n_sl, win[t].color_hash)) {
						in_shortlist[t] = 1;
						continue;
					}
					in_shortlist[t] = 1;
					shortlist[n_sl++] = t;
				}
			}
		}
	}
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_residual += t1 - t0;

	/* Up to n_rand pool blocks join the shortlist. Utility decides whether they install. */
	t0 = stvq_ns_now();
	rng = frame_index * 2654435761u + 0x9E3779B9u;
	for (i = 0; i < n_rand && n_sl < shortlist_cap; i++) {
		unsigned tries;
		int added = 0;
		for (tries = 0; tries < pool_n; tries++) {
			unsigned wi = pool_base + (xorshift32(&rng) % pool_n);
			unsigned src_frame = frame_index + wi / tiles_n;
			int next_epoch = (cut_at && src_frame >= cut_at);
			uint32_t epoch = cb->pal_epoch + (next_epoch ? 1u : 0u);
			if (in_shortlist[wi])
				continue;
			if (shortlist_has_hash(win, shortlist, n_sl, win[wi].color_hash)) {
				in_shortlist[wi] = 1;
				continue;
			}
			if (epoch_has_hash(cb, hash_post, epoch, win[wi].color_hash))
				continue;
			in_shortlist[wi] = 1;
			shortlist[n_sl++] = wi;
			added = 1;
			break;
		}
		if (!added)
			break;
	}
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_random += t1 - t0;

	/* Pre-install residual. Stage 2 mutates win[].dist as tiles are installed. */
	if (out_win_dist || out_win_pool) {
		unsigned wi;
		if (out_win_dist) {
			for (wi = 0; wi < nframes * tiles_n; wi++)
				out_win_dist[wi] = ~0u;
			for (wi = 0; wi < win_n; wi++) {
				unsigned fr = frame_index + wi / tiles_n;
				unsigned tile = wi % tiles_n;
				unsigned dist = win[wi].dist;
				if (fr >= pool_lo && fr <= f_end)
					dist = eff_dist[wi];
				out_win_dist[fr * tiles_n + tile] = dist;
			}
		}
		if (out_win_pool) {
			memset(out_win_pool, 0, nframes);
			for (f = pool_lo; f <= f_end; f++)
				out_win_pool[f] = 1;
		}
	}

	/*
	 * Stage 2 (decoupled):
	 * - Score the remaining shortlist by predicted add-utility (ignore eviction).
	 * - Walk that ranking. Install a tile while its realized utility, measured
	 *   on the book after earlier installs in this run, is at least half what
	 *   this ranking predicted. The first tile always qualifies.
	 * - The first tile below half is left uninstalled and the shortlist is
	 *   scored again. Each install picks a fresh least-damage victim.
	 */
	t0 = stvq_ns_now();
	{
		unsigned quota = n_util;
		unsigned pass = 0;
		int stop = 0;
		uint64_t ts, te;
		float feat_cur[STVQ_METRIC_MAX_FEAT_LEN], feat_post[STVQ_METRIC_MAX_FEAT_LEN];
		uint32_t hash_cur, hash_new_post;

		while (quota > 0 && !stop) {
			unsigned n_cand = 0, vi;

			ts = stvq_ns_now();
			for (i = 0; i < n_sl; i++) {
				unsigned wi = shortlist[i];
				unsigned src_frame = frame_index + wi / tiles_n;
				unsigned tile = wi % tiles_n;
				int next_epoch = (cut_at && src_frame >= cut_at);
				long long util;
				const uint8_t *tile32 = frame_tiles[src_frame] + tile * 32u;
				if (tile_taken[wi])
					continue;
				tile_feats_cur_post(tile32, feat_cur, feat_post, &hash_cur, &hash_new_post, feats_post,
				    post_pal, post_subset, cur_pal, cur_subset);
				util = add_utility(win, win_n, feat_cur, feats_post ? feat_post : NULL, hash_cur,
				    hash_new_post, cb->feats, d_new, next_epoch, prof);
				if (util <= 0)
					continue;
				ucand[n_cand].tile = wi;
				ucand[n_cand].util = util;
				n_cand++;
			}
			te = stvq_ns_now();
			if (prof)
				prof->ns_util_score += te - ts;
			if (n_cand == 0)
				break;
			qsort(ucand, n_cand, sizeof(UtilCand), util_cand_cmp_desc);

			ts = stvq_ns_now();
			for (vi = 0; vi < n_cand && quota > 0; vi++) {
				unsigned wi = ucand[vi].tile;
				unsigned src_frame = frame_index + wi / tiles_n;
				unsigned tile = wi % tiles_n;
				int next_epoch = (cut_at && src_frame >= cut_at);
				uint32_t epoch = cb->pal_epoch + (next_epoch ? 1u : 0u);
				long long actual;
				unsigned victim;
				uint64_t tv;
				const uint8_t *tile32 = frame_tiles[src_frame] + tile * 32u;
				tile_feats_cur_post(tile32, feat_cur, feat_post, &hash_cur, &hash_new_post, feats_post,
				    post_pal, post_subset, cur_pal, cur_subset);
				actual = add_utility(win, win_n, feat_cur, feats_post ? feat_post : NULL, hash_cur,
				    hash_new_post, cb->feats, d_new, next_epoch, prof);
				if (actual * 2 < ucand[vi].util) {
					/* Predicted ranking is stale. Re-score; do not install. */
					note_util(prof, pass, STVI_CB_NONE, wi, ucand[vi].util, actual);
					if (vi == 0)
						stop = 1;
					break;
				}
				tv = stvq_ns_now();
				accumulate_evict_damage(win, win_n, damage, cb->entries);
				if (prof)
					prof->ns_victim_scan += stvq_ns_now() - tv;
				victim = pick_least_damage_victim(cb, damage, slot_taken, now, prof);
				if (!install_at_victim(cb, tile32, wi, victim, now, win, win_n, slot_taken, tile_taken, d_new,
				        feats_post, hash_post, post_pal, post_subset, cur_pal, cur_subset, epoch, next_epoch,
				        out, &n)) {
					stop = 1;
					break;
				}
				note_util(prof, pass, victim, wi, ucand[vi].util, actual);
				quota--;
			}
			te = stvq_ns_now();
			if (prof)
				prof->ns_util_install += te - ts;
			pass++;
		}
		if (prof)
			prof->nscore = pass;
	}
	t1 = stvq_ns_now();
	if (prof)
		prof->ns_utility += t1 - t0;

	for (t = 0; t < tiles_n; t++) {
		unsigned ni, nd;
		/* STVD may only reference current-palette CB entries. */
		ni = nearest_current_feat(cb, win[t].feat, win[t].color_hash, &nd);
		cb->use_count[ni]++;
		cb->last_used[ni] = now;
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
	free(win);
	free(win_feats);
	free(d_new);
	free(eff_dist);
	free(damage);
	free(feats_post);
	free(hash_post);
	return n;
}
