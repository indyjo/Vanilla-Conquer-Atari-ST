/*
 * LRU-cached planar + 1bpp mask sprite cache for Buffer_Frame_To_Page (Atari ST).
 */

#include "st_sprite_cache.h"

#include "c2p.h"
#include "st_blitter_blit.h"

#include "lrucache.h"

#include <stdint.h>

#include <cstdlib>
#include <cstring>
#include <new>

#ifndef ST_SPRITE_CACHE_CAPACITY_16
#define ST_SPRITE_CACHE_CAPACITY_16 160
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_32
#define ST_SPRITE_CACHE_CAPACITY_32 40
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_64
#define ST_SPRITE_CACHE_CAPACITY_64 24
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_96
#define ST_SPRITE_CACHE_CAPACITY_96 6
#endif
/* Hard clamp for per-tier slot count (LRU uses uint16_t slot indices). */
#ifndef ST_SPRITE_CACHE_TIER_CAP_MAX
#define ST_SPRITE_CACHE_TIER_CAP_MAX 4096
#endif
enum { SPRITE_CACHE_D16 = 16, SPRITE_CACHE_D32 = 32, SPRITE_CACHE_D64 = 64, SPRITE_CACHE_D96 = 96 };

/*
 * Run lazy_frame_fill (Build_Frame) at most once per planar composite; only LRU miss triggers fill.
 */
/* One-shot decode gate for miss-time lazy frame build. */
struct SpriteCacheLazyGate {
	unsigned long (*fill)(void *ctx);
	void *ctx;
	unsigned char decoded; /* 1 after first successful fill */
};

/*
 * UnitShadow ghost rows (see DISPLAY.CPP Conquer_Build_Translucent_Table) fade LTGREEN→BLACK against
 * a logical backdrop palette index; without reading the framebuffer, use BLACK (EGA logical 12).
 */
enum { SPRITE_CACHE_GHOST_SYNTH_BACKDROP_IX = 12 };

static inline uint32_t sprite_cache_mix32(uint32_t x);
static inline uint32_t sprite_cache_mix32_pair(uint32_t a, uint32_t b);
static inline uint32_t sprite_cache_rotl32(uint32_t x, unsigned r);

/* Logical cache key: sprite identity plus render-variant discriminators. */
struct SpriteCacheKey {
	uint32_t src_key; /* sprite identity only (+ remap tokens folded in dispatch) */
	uint8_t mode_pack; /* 0 plain, 1 fade, 2 ghost */
	bool trans_flag;
	uint32_t fade_token; /* 0 none; else stable mix of remap table identity (not raw pointers) */
	uint32_t ghost_token;

	bool operator==(const SpriteCacheKey &o) const
	{
		return src_key == o.src_key && mode_pack == o.mode_pack && trans_flag == o.trans_flag
		       && fade_token == o.fade_token && ghost_token == o.ghost_token;
	}
};

/* Stateless hash functor for SpriteCacheKey lookups in tier LRUs. */
struct SpriteCacheKeyHash {
	size_t operator()(const SpriteCacheKey &k) const
	{
		uint32_t h = k.src_key;
		h = sprite_cache_mix32_pair(h, k.fade_token);
		h = sprite_cache_mix32_pair(h, k.ghost_token);
		h = sprite_cache_mix32_pair(h, (uint32_t)((unsigned)k.mode_pack << 8));
		h = sprite_cache_mix32_pair(h, k.trans_flag ? 0xdeadbeefu : 0u);
		return (size_t)h;
	}
};

/* Per-slot crop metadata in full-frame coordinates plus occupancy bit. */
struct SpriteCacheSlotMeta {
	uint16_t crop_x;
	uint16_t crop_y;
	uint16_t crop_w;
	uint16_t crop_h;
	uint8_t occupied;
};

/* One configured tier pool (memory slices, and LRU map). */
struct SpriteCacheTier {
	/* Nominal template side (16/32/64/96): slot planar+mask sizes match a square that size; picks are by bytes. */
	int dim = 0;
	int capacity = 0;
	uint8_t *planar_base = nullptr;
	uint8_t *mask_base = nullptr;
	int planar_bpl = 0;
	int planar_slot_sz = 0;
	int mask_bpl = 0;
	int mask_slot_sz = 0;
	LruCache<SpriteCacheKey, uint16_t, SpriteCacheKeyHash> *lru = nullptr;
	SpriteCacheSlotMeta *slot_meta = nullptr;
};

/* Per-tier runtime counters used by debug stats hotkey dump. */
struct SpriteCacheTierStats {
	unsigned long hits;
	unsigned long misses;
	unsigned long evictions;
	unsigned long fills_fast;
	unsigned long fills_slow;
	unsigned long filled_pixels_sum;
};

static SpriteCacheTier g_sprite_cache_tiers[4];
static uint8_t *g_sprite_cache_slab = nullptr;
static bool g_sprite_cache_inited = false;
static int g_sprite_cache_cap[4] = { ST_SPRITE_CACHE_CAPACITY_16, ST_SPRITE_CACHE_CAPACITY_32,
	ST_SPRITE_CACHE_CAPACITY_64, ST_SPRITE_CACHE_CAPACITY_96 };
static SpriteCacheTierStats g_sprite_cache_stats[4];

/* 32-bit rotate-left helper used by lightweight hash mixers. */
static inline uint32_t sprite_cache_rotl32(uint32_t x, unsigned r)
{
	return (uint32_t)((x << r) | (x >> (32u - r)));
}

/* Cheap multiply-free 32-bit avalanche mix (small rotates/shifts only). */
static inline uint32_t sprite_cache_mix32(uint32_t x)
{
	x ^= sprite_cache_rotl32(x, 3);
	x += (x << 2);
	x ^= (x >> 5);
	x += sprite_cache_rotl32(x, 7);
	x ^= (x >> 3);
	return x;
}

/* Combines two words with small rotates and one avalanche step. */
static inline uint32_t sprite_cache_mix32_pair(uint32_t a, uint32_t b)
{
	uint32_t h = a ^ sprite_cache_rotl32(b, 5);
	h += sprite_cache_rotl32(a, 2);
	h ^= (b >> 3);
	return sprite_cache_mix32(h);
}

/*
 * 32-bit token for distinguishing objects by pointer identity without storing naked addresses in LRU keys.
 */
static uint32_t sprite_cache_ptr_token(void const *p)
{
	if (p == nullptr) {
		return 0;
	}
	uintptr_t const u = (uintptr_t)(void const *)p;
	uint32_t h = sprite_cache_mix32((uint32_t)u);
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ >= 8
	h = sprite_cache_mix32_pair(h, (uint32_t)(((unsigned long long)u) >> 32));
#endif
	return h;
}

extern "C" long ST_SPRITE_CACHE_Frame_Identity_Key(void const *blobs_root, int frame_index)
{
	if (!blobs_root || frame_index < 0) {
		return 0L;
	}
	uint32_t h = sprite_cache_ptr_token(blobs_root);
	h = sprite_cache_mix32_pair(h, ((uint32_t)(unsigned short)frame_index) ^ 419513369u);
	return (long)(unsigned long)(unsigned)h;
}

/* Sprite identity only; geometry is stored as slot metadata after crop scan. */
static uint32_t sprite_cache_lru_identity_hash(long identity_key)
{
	uint32_t h = 0x9e3779b9u;

	if (identity_key != (long)0) {
#if defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ >= 8)
		h = sprite_cache_mix32_pair(h, (uint32_t)(unsigned long)identity_key);
		h = sprite_cache_mix32_pair(h, (uint32_t)(((unsigned long)identity_key) >> 32));
#else
		h = sprite_cache_mix32_pair(h, (uint32_t)(unsigned long)identity_key);
#endif
	}
	return h;
}

/*
 * Write one 16-color ST low-res planar pixel into a BFTP scratch slot (same word/bit layout
 * as the screen buffer the blitter reads). Used on LRU fill when trans/fade/ghost prevent bulk C2P;
 * caller passes the final display nibble (typically C2P_Map8ToPlanar4 on sprite-local coords).
 * Clamps by returning if (x,y) is outside width_px × height_px.
 */
static inline void sprite_cache_scratch_put_px(
	uint8_t *base, int row_bytes, int width_px, int height_px, int x, int y, unsigned char color4)
{
	if (!base || x < 0 || y < 0 || x >= width_px || y >= height_px || row_bytes <= 0)
		return;
	uint8_t *p = base + y * row_bytes + (x >> 4) * 8 + ((x >> 3) & 1);
	const int bitnum = 7 - (x & 7);
	const uint8_t maskbit = (uint8_t)(1u << (unsigned)bitnum);
	const uint8_t c = (uint8_t)(color4 & 15);
	for (int pl = 0; pl < 4; pl++) {
		uint8_t *pb = p + pl * 2;
		if (c & (uint8_t)(1u << pl))
			*pb |= maskbit;
		else
			*pb &= (uint8_t)~maskbit;
	}
}

/* Clear mask bit → ST blitter clears dest before OR-merge. Leave 1 to preserve backdrop. */
static inline void sprite_cache_mask_mark_writes(uint8_t *maskbm, int rowb, int x, int y)
{
	uint8_t *b = maskbm + (size_t)y * (size_t)rowb + (size_t)(x >> 3);
	*b = (uint8_t)(*b & (uint8_t)~(0x80u >> (x & 7)));
}

/*
 * ST interleaved planar + 1bpp mask layout for a tight crop: width rounded up to 16 px, height = crop_h.
 * Row strides match the blitter / C2P helpers (8 bytes per 16 horizontal pixels in planar, 2 in mask).
 */
static void sprite_cache_layout_for_crop(int crop_w, int crop_h, int *out_planar_w, int *out_planar_h,
	int *out_planar_rowb, int *out_mask_rowb, size_t *out_planar_bytes, size_t *out_mask_bytes)
{
	const int planar_w = ((crop_w + 15) >> 4) << 4;
	const int planar_h = crop_h > 0 ? crop_h : 0;
	if (out_planar_w)
		*out_planar_w = planar_w;
	if (out_planar_h)
		*out_planar_h = planar_h;
	if (planar_w <= 0 || planar_h <= 0) {
		if (out_planar_rowb)
			*out_planar_rowb = 0;
		if (out_mask_rowb)
			*out_mask_rowb = 0;
		if (out_planar_bytes)
			*out_planar_bytes = 0;
		if (out_mask_bytes)
			*out_mask_bytes = 0;
		return;
	}
	const int words = planar_w >> 4;
	const int planar_rowb = words * 8;
	const int mask_rowb = words * 2;
	if (out_planar_rowb)
		*out_planar_rowb = planar_rowb;
	if (out_mask_rowb)
		*out_mask_rowb = mask_rowb;
	if (out_planar_bytes)
		*out_planar_bytes = (size_t)planar_rowb * (size_t)planar_h;
	if (out_mask_bytes)
		*out_mask_bytes = (size_t)mask_rowb * (size_t)planar_h;
}

/* Returns tier object by fixed tier index (0..3). */
static SpriteCacheTier *sprite_cache_tier_from_index(int idx)
{
	if (idx < 0 || idx >= 4)
		return nullptr;
	if (!g_sprite_cache_tiers[idx].lru)
		return nullptr;
	return &g_sprite_cache_tiers[idx];
}

/* Pick smallest slab (by allocated planar+mask slot bytes) whose per-slot buffers fit the crop layout. */
static int sprite_cache_pick_tier_index_for_crop(int crop_w, int crop_h)
{
	if (crop_w < 0 || crop_h < 0)
		return -1;
	if (crop_w == 0 || crop_h == 0) {
		/* Fully transparent: use smallest enabled pool so retarget still has a slot. */
		int best_i = -1;
		size_t best_total = 0;
		for (int i = 0; i < 4; ++i) {
			SpriteCacheTier *tr = sprite_cache_tier_from_index(i);
			if (!tr || tr->capacity <= 0)
				continue;
			const size_t tot = (size_t)tr->planar_slot_sz + (size_t)tr->mask_slot_sz;
			if (best_i < 0 || tot < best_total) {
				best_i = (int)i;
				best_total = tot;
			}
		}
		return best_i;
	}

	size_t need_p = 0;
	size_t need_m = 0;
	sprite_cache_layout_for_crop(crop_w, crop_h, nullptr, nullptr, nullptr, nullptr, &need_p, &need_m);

	int best_i = -1;
	size_t best_total = 0;
	for (int i = 0; i < 4; ++i) {
		SpriteCacheTier *tr = sprite_cache_tier_from_index(i);
		if (!tr || tr->capacity <= 0)
			continue;
		if (need_p > (size_t)tr->planar_slot_sz || need_m > (size_t)tr->mask_slot_sz)
			continue;
		const size_t tot = (size_t)tr->planar_slot_sz + (size_t)tr->mask_slot_sz;
		if (best_i < 0 || tot < best_total) {
			best_i = (int)i;
			best_total = tot;
		}
	}
	return best_i;
}

/* Resets runtime stats counters without touching cache contents. */
static void sprite_cache_reset_stats(void)
{
	std::memset(g_sprite_cache_stats, 0, sizeof(g_sprite_cache_stats));
}

/*
 * Tight bounds for index-0 transparency via edge scan: top/bottom rows, then left/right
 * columns between those rows (one bound updated per pass). Sets *out_w/*out_h to 0 when empty.
 */
static void sprite_cache_scan_transparent_crop_edges(const uint8_t *src,
	int full_w,
	int full_h,
	int stride,
	int *out_x,
	int *out_y,
	int *out_w,
	int *out_h)
{
	int min_y = full_h;
	for (int y = 0; y < full_h; ++y) {
		const uint8_t *row = src + (size_t)y * (size_t)stride;
		int x = 0;
		for (; x < full_w; ++x) {
			if (row[x] != 0)
				break;
		}
		if (x < full_w) {
			min_y = y;
			break;
		}
	}
	if (min_y >= full_h) {
		*out_x = 0;
		*out_y = 0;
		*out_w = 0;
		*out_h = 0;
		return;
	}

	int max_y = -1;
	for (int y = full_h - 1; y >= min_y; --y) {
		const uint8_t *row = src + (size_t)y * (size_t)stride;
		int x = 0;
		for (; x < full_w; ++x) {
			if (row[x] != 0)
				break;
		}
		if (x < full_w) {
			max_y = y;
			break;
		}
	}

	int min_x = full_w;
	for (int x = 0; x < full_w; ++x) {
		int y = min_y;
		for (; y <= max_y; ++y) {
			if (src[(size_t)y * (size_t)stride + (size_t)x] != 0)
				break;
		}
		if (y <= max_y) {
			min_x = x;
			break;
		}
	}

	int max_x = -1;
	for (int x = full_w - 1; x >= min_x; --x) {
		int y = min_y;
		for (; y <= max_y; ++y) {
			if (src[(size_t)y * (size_t)stride + (size_t)x] != 0)
				break;
		}
		if (y <= max_y) {
			max_x = x;
			break;
		}
	}

	if (max_x < min_x || max_y < min_y) {
		*out_x = 0;
		*out_y = 0;
		*out_w = 0;
		*out_h = 0;
		return;
	}

	*out_x = min_x;
	*out_y = min_y;
	*out_w = max_x - min_x + 1;
	*out_h = max_y - min_y + 1;
}

/* Finds tight non-transparent bounds in decoded chunky sprite data. */
static void sprite_cache_scan_crop_bounds(const uint8_t *src,
	int full_w,
	int full_h,
	int stride,
	int trans,
	int *out_x,
	int *out_y,
	int *out_w,
	int *out_h)
{
	if (!src || full_w <= 0 || full_h <= 0 || stride <= 0 || !out_x || !out_y || !out_w || !out_h) {
		return;
	}

	if (!trans) {
		*out_x = 0;
		*out_y = 0;
		*out_w = full_w;
		*out_h = full_h;
		return;
	}

	sprite_cache_scan_transparent_crop_edges(src, full_w, full_h, stride, out_x, out_y, out_w, out_h);
}

/* Lazy one-time cache slab and per-tier allocator init. */
static void sprite_cache_apply_default_caps(void);
static void sprite_cache_shutdown(void);
static void sprite_cache_maybe_init(void);

void ST_SPRITE_CACHE_Init(void)
{
	sprite_cache_maybe_init();
}

static void sprite_cache_apply_default_caps(void)
{
	g_sprite_cache_cap[0] = ST_SPRITE_CACHE_CAPACITY_16;
	g_sprite_cache_cap[1] = ST_SPRITE_CACHE_CAPACITY_32;
	g_sprite_cache_cap[2] = ST_SPRITE_CACHE_CAPACITY_64;
	g_sprite_cache_cap[3] = ST_SPRITE_CACHE_CAPACITY_96;
}

static void sprite_cache_shutdown(void)
{
	for (int t = 0; t < 4; ++t) {
		SpriteCacheTier &tr = g_sprite_cache_tiers[t];
		delete tr.lru;
		tr.lru = nullptr;
		delete[] tr.slot_meta;
		tr.slot_meta = nullptr;
		tr.planar_base = nullptr;
		tr.mask_base = nullptr;
		tr.capacity = 0;
		tr.dim = 0;
		tr.planar_bpl = 0;
		tr.mask_bpl = 0;
		tr.planar_slot_sz = 0;
		tr.mask_slot_sz = 0;
	}
	std::free(g_sprite_cache_slab);
	g_sprite_cache_slab = nullptr;
	g_sprite_cache_inited = false;
	sprite_cache_reset_stats();
}

static void sprite_cache_maybe_init(void)
{
	if (g_sprite_cache_inited)
		return;

	const int dims[4] = { SPRITE_CACHE_D16, SPRITE_CACHE_D32, SPRITE_CACHE_D64, SPRITE_CACHE_D96 };
	int planar_bp[4], mask_bp[4], ps_sz[4], ms_sz[4];
	size_t total = 0;

	for (int t = 0; t < 4; ++t) {
		const int d = dims[t];
		planar_bp[t] = ((d + 15) >> 4) * 8;
		mask_bp[t] = ((d + 15) >> 4) * 2;
		ps_sz[t] = planar_bp[t] * d;
		ms_sz[t] = mask_bp[t] * d;
		const int cap = g_sprite_cache_cap[t];
		if (cap > 0) {
			total += (size_t)cap * (size_t)ps_sz[t];
			total += (size_t)cap * (size_t)ms_sz[t];
		}
	}

	if (total == 0)
		return;

	g_sprite_cache_slab = (uint8_t *)std::malloc(total);
	if (!g_sprite_cache_slab)
		return;

	uint8_t *walk = g_sprite_cache_slab;
	for (int t = 0; t < 4; ++t) {
		SpriteCacheTier &tr = g_sprite_cache_tiers[t];
		tr.dim = dims[t];
		tr.capacity = g_sprite_cache_cap[t];
		tr.planar_bpl = planar_bp[t];
		tr.mask_bpl = mask_bp[t];
		tr.planar_slot_sz = ps_sz[t];
		tr.mask_slot_sz = ms_sz[t];
		tr.lru = nullptr;
		tr.slot_meta = nullptr;

		if (tr.capacity <= 0) {
			tr.planar_base = nullptr;
			tr.mask_base = nullptr;
			continue;
		}

		tr.planar_base = walk;
		walk += (size_t)tr.capacity * (size_t)tr.planar_slot_sz;
		tr.mask_base = walk;
		walk += (size_t)tr.capacity * (size_t)tr.mask_slot_sz;

		tr.lru =
		    new (std::nothrow) LruCache<SpriteCacheKey, uint16_t, SpriteCacheKeyHash>((size_t)tr.capacity);
		if (!tr.lru)
			goto fail;
		tr.slot_meta = new (std::nothrow) SpriteCacheSlotMeta[(size_t)tr.capacity];
		if (!tr.slot_meta)
			goto fail;
		for (uint16_t i = 0; i < (uint16_t)tr.capacity; ++i) {
			SpriteCacheKey dk;
			std::memset(&dk, 0, sizeof(dk));
			dk.src_key = UINT32_MAX ^ ((((uint32_t)t) << 20) ^ (uint32_t)i);
			dk.mode_pack = 0xFF;
			dk.fade_token = (uint32_t)t + 1u;
			dk.ghost_token = 0x1000u + (uint32_t)i;
			dk.trans_flag = false;
			tr.lru->put(dk, i);
			tr.slot_meta[i].crop_x = 0;
			tr.slot_meta[i].crop_y = 0;
			tr.slot_meta[i].crop_w = 0;
			tr.slot_meta[i].crop_h = 0;
			tr.slot_meta[i].occupied = 0;
		}
	}

	g_sprite_cache_inited = true;
	return;
fail:
	sprite_cache_shutdown();
}

extern "C" int ST_SPRITE_CACHE_Reconfigure_TierCapacities(int c16, int c32, int c64, int c96)
{
	int caps[4] = { c16, c32, c64, c96 };
	for (int i = 0; i < 4; ++i) {
		if (caps[i] < 0)
			return -1;
		if (caps[i] > ST_SPRITE_CACHE_TIER_CAP_MAX)
			caps[i] = ST_SPRITE_CACHE_TIER_CAP_MAX;
	}
	int sum = 0;
	for (int i = 0; i < 4; ++i)
		sum += caps[i];
	if (sum <= 0)
		return -1;

	sprite_cache_shutdown();
	for (int i = 0; i < 4; ++i)
		g_sprite_cache_cap[i] = caps[i];
	sprite_cache_maybe_init();
	return g_sprite_cache_inited ? 0 : -1;
}

extern "C" void ST_SPRITE_CACHE_Reset_Tier_Capacities_To_Defaults(void)
{
	sprite_cache_apply_default_caps();
	sprite_cache_shutdown();
	sprite_cache_maybe_init();
}

/* Executes either opaque or mask+OR blitter sequence. */
static BOOL sprite_cache_do_blitter(
	BOOL use_trans_merge,
	uint8_t *dst_root_fb,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int dx_abs,
	int dy_abs,
	const uint8_t *planar,
	int planar_rowb,
	const uint8_t *maskbm,
	int mask_rowb,
	int src_w,
	int src_h,
	int sx_abs,
	int sy_abs,
	int blit_w,
	int blit_h)
{
	if (use_trans_merge) {
		if (!ST_Blitter_Mask_And_Planar_Rect(
			maskbm,
			mask_rowb,
			src_w,
			src_h,
			sx_abs,
			sy_abs,
			dst_root_fb,
			dst_row_bytes,
			dst_width_pixels,
			dst_height_pixels,
			dx_abs,
			dy_abs,
			blit_w,
			blit_h))
			return FALSE;
		if (!ST_Blitter_Planar_Rect_Blit_Or(planar,
				planar_rowb,
				src_w,
				src_h,
				sx_abs,
				sy_abs,
				dst_root_fb,
				dst_row_bytes,
				dst_width_pixels,
				dst_height_pixels,
				dx_abs,
				dy_abs,
				blit_w,
			blit_h))
			return FALSE;
		return TRUE;
	}
	return ST_Blitter_Planar_Rect_Blit(planar,
		   planar_rowb,
		   src_w,
		   src_h,
		   sx_abs,
		   sy_abs,
		   dst_root_fb,
		   dst_row_bytes,
		   dst_width_pixels,
		   dst_height_pixels,
		   dx_abs,
		   dy_abs,
		   blit_w,
		   blit_h)
		       ? TRUE
		       : FALSE;
}

/* Fills one cache slot from cropped source pixels and optional remaps.
 * Returns 1 = fast path (bulk C2P), 2 = slow path (per-pixel scratch), 0 = failure. */
static int sprite_cache_fill_slot_pixels(
	SpriteCacheTier *tr,
	uint16_t slot,
	uint8_t *dst_root_fb,
	const uint8_t *src,
	int bw,
	int bh,
	int stride,
	int ax0,
	int ay0,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	int crop_x,
	int crop_y,
	int crop_w,
	int crop_h)
{
	uint8_t *planar = tr->planar_base + (size_t)slot * (size_t)tr->planar_slot_sz;
	uint8_t *maskbm = tr->mask_base + (size_t)slot * (size_t)tr->mask_slot_sz;

	std::memset(planar, 0, (size_t)tr->planar_slot_sz);
	std::memset(maskbm, 0xFF, (size_t)tr->mask_slot_sz);

	if (crop_w <= 0 || crop_h <= 0)
		return 1;
	if (crop_x < 0 || crop_y < 0 || crop_x + crop_w > bw || crop_y + crop_h > bh)
		return 0;

	int scratch_w = 0;
	int scratch_h = 0;
	int planar_rowb = 0;
	int mask_rowb = 0;
	size_t need_p = 0;
	size_t need_m = 0;
	sprite_cache_layout_for_crop(
	    crop_w, crop_h, &scratch_w, &scratch_h, &planar_rowb, &mask_rowb, &need_p, &need_m);
	if (scratch_w <= 0 || scratch_h <= 0)
		return 1;
	if (need_p > (size_t)tr->planar_slot_sz || need_m > (size_t)tr->mask_slot_sz)
		return 0;

	/* Fully opaque chunky → scratch planar fast path */
	if (!ghost_tab && !fade_tab && !trans) {
		/* abs_x0/abs_y0 = 0: Bayer phase from sprite-local (col,row) only — cache is placement-agnostic. */
		C2P_Render_Logical_To_Planar_Rect(
			src + (size_t)crop_y * (size_t)stride + (size_t)crop_x,
			crop_w,
			crop_h,
			stride,
			planar,
			planar_rowb,
			scratch_w,
			scratch_h,
			0,
			0,
			0,
			0);
		return 1;
	}

	(void)dst_root_fb;
	(void)ax0;
	(void)ay0;

	const BOOL masked_merge = (ghost_tab != nullptr) || (trans != 0);
	const uint8_t *const ghost_cls = ghost_tab;
	const uint8_t *const ghost_blend = ghost_cls ? ghost_cls + 256 : nullptr;

	for (int row = crop_y; row < crop_y + crop_h; ++row) {
		const uint8_t *srow = src + (size_t)row * (size_t)stride;
		for (int col = crop_x; col < crop_x + crop_w; ++col) {
			const uint8_t raw = srow[col];
			if (trans && raw == 0)
				continue;

			unsigned char pal8 = raw;

			if (ghost_cls && ghost_blend) {
				const uint8_t cls = ghost_cls[raw];
				if (cls != 0xFFu) {
					/*
					 * Sprite-local checkerboard mask dither (~50%); must not use screen coords
					 * so cached fills are valid at any placement.
					 */
					if (((col ^ row) & 1) != 0)
						continue;
					pal8 = ghost_blend[(size_t)cls * 256u + SPRITE_CACHE_GHOST_SYNTH_BACKDROP_IX];
				} else {
					pal8 = fade_tab ? fade_tab[raw] : raw;
				}
			} else if (fade_tab) {
				pal8 = fade_tab[raw];
			}

			const int sx = col - crop_x;
			const int sy = row - crop_y;
			const unsigned char c4 = C2P_Map8ToPlanar4(sx, sy, pal8);
			sprite_cache_scratch_put_px(planar, planar_rowb, scratch_w, scratch_h, sx, sy, c4);
			if (masked_merge)
				sprite_cache_mask_mark_writes(maskbm, mask_rowb, sx, sy);
		}
	}
	return 2;
}

/* Probes tiers, fills on miss, and blits cropped intersection. */
static long sprite_cache_cached_tile_dispatch(uint8_t *dst_root_fb,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int ax0,
	int ay0,
	int clip_w,
	int clip_h,
	int src_stride,
	int full_w,
	int full_h,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	const uint8_t *raster_base,
	int clip_ox,
	int clip_oy,
	long identity_key,
	SpriteCacheLazyGate *lazy_gate)
{
	uint8_t mode_pack = 0;
	if (ghost_tab)
		mode_pack = 2;
	else if (fade_tab)
		mode_pack = 1;
	else
		mode_pack = 0;

	SpriteCacheKey want;
	want.src_key = sprite_cache_lru_identity_hash(identity_key);
	want.mode_pack = mode_pack;
	want.trans_flag = trans != 0;
	want.fade_token = sprite_cache_ptr_token((void const *)fade_tab);
	want.ghost_token = sprite_cache_ptr_token((void const *)ghost_tab);

	uint16_t slot = 0;
	SpriteCacheTier *tr = nullptr;
	SpriteCacheSlotMeta meta;
	bool cache_hit = false;

	for (int ti = 0; ti < 4; ++ti) {
		SpriteCacheTier *probe = sprite_cache_tier_from_index(ti);
		if (!probe || !probe->lru || !probe->slot_meta)
			continue;
		if (probe->lru->get(want, slot)) {
			g_sprite_cache_stats[ti].hits++;
			tr = probe;
			meta = probe->slot_meta[slot];
			cache_hit = true;
			break;
		}
		g_sprite_cache_stats[ti].misses++;
	}

	if (!cache_hit) {
		if (lazy_gate != nullptr && lazy_gate->fill != nullptr && lazy_gate->decoded == 0) {
			unsigned long const built = lazy_gate->fill(lazy_gate->ctx);
			if (built == 0UL) {
				return 0;
			}
			if ((const uint8_t *)(uintptr_t)built != raster_base) {
				return 0;
			}
			lazy_gate->decoded = 1;
		}

		int crop_x = 0;
		int crop_y = 0;
		int crop_w = full_w;
		int crop_h = full_h;
		sprite_cache_scan_crop_bounds(
		    raster_base, full_w, full_h, src_stride, trans, &crop_x, &crop_y, &crop_w, &crop_h);

		const int tier_ix = sprite_cache_pick_tier_index_for_crop(crop_w, crop_h);
		if (tier_ix < 0) {
			return 0;
		}

		tr = sprite_cache_tier_from_index(tier_ix);
		if (!tr || !tr->lru || !tr->slot_meta) {
			return 0;
		}
		if (!tr->lru->retarget_oldest_slot(want, slot)) {
			return 0;
		}

		if (tr->slot_meta[slot].occupied) {
			const int ti_stat = (int)(tr - g_sprite_cache_tiers);
			if (ti_stat >= 0 && ti_stat < 4)
				g_sprite_cache_stats[ti_stat].evictions++;
		}

		const int fill_route = sprite_cache_fill_slot_pixels(
			    tr,
			    slot,
			    dst_root_fb,
			    raster_base,
			    full_w,
			    full_h,
			    src_stride,
			    ax0,
			    ay0,
			    trans,
			    ghost_tab,
			    fade_tab,
			    crop_x,
			    crop_y,
			    crop_w,
			    crop_h);
		if (fill_route == 0) {
			return 0;
		}

		meta.crop_x = (uint16_t)crop_x;
		meta.crop_y = (uint16_t)crop_y;
		meta.crop_w = (uint16_t)crop_w;
		meta.crop_h = (uint16_t)crop_h;
		meta.occupied = 1;
		tr->slot_meta[slot] = meta;
		for (int ti = 0; ti < 4; ++ti) {
			if (&g_sprite_cache_tiers[ti] == tr) {
				if (fill_route == 1)
					g_sprite_cache_stats[ti].fills_fast++;
				else
					g_sprite_cache_stats[ti].fills_slow++;
				g_sprite_cache_stats[ti].filled_pixels_sum += (unsigned long)crop_w * (unsigned long)crop_h;
				break;
			}
		}
	}

	const int req_x0 = clip_ox;
	const int req_y0 = clip_oy;
	const int req_x1 = clip_ox + clip_w;
	const int req_y1 = clip_oy + clip_h;
	const int crop_x0 = (int)meta.crop_x;
	const int crop_y0 = (int)meta.crop_y;
	const int crop_x1 = crop_x0 + (int)meta.crop_w;
	const int crop_y1 = crop_y0 + (int)meta.crop_h;

	int ix0 = req_x0 > crop_x0 ? req_x0 : crop_x0;
	int iy0 = req_y0 > crop_y0 ? req_y0 : crop_y0;
	int ix1 = req_x1 < crop_x1 ? req_x1 : crop_x1;
	int iy1 = req_y1 < crop_y1 ? req_y1 : crop_y1;
	if (ix1 <= ix0 || iy1 <= iy0) {
		return 0;
	}

	const int draw_w = ix1 - ix0;
	const int draw_h = iy1 - iy0;
	const int dst_x = ax0 + (ix0 - clip_ox);
	const int dst_y = ay0 + (iy0 - clip_oy);
	const int src_x = ix0 - crop_x0;
	const int src_y = iy0 - crop_y0;

	const uint8_t *planar = tr->planar_base + (size_t)slot * (size_t)tr->planar_slot_sz;
	const uint8_t *maskbm = tr->mask_base + (size_t)slot * (size_t)tr->mask_slot_sz;

	int scratch_w = 0;
	int scratch_h = 0;
	int planar_rowb = 0;
	int mask_rowb = 0;
	sprite_cache_layout_for_crop(
	    (int)meta.crop_w, (int)meta.crop_h, &scratch_w, &scratch_h, &planar_rowb, &mask_rowb, nullptr, nullptr);

	const BOOL use_merge = (trans != 0 || ghost_tab != nullptr) ? TRUE : FALSE;

	if (!sprite_cache_do_blitter(use_merge ? TRUE : FALSE,
		    dst_root_fb,
		    dst_row_bytes,
		    dst_width_pixels,
		    dst_height_pixels,
		    dst_x,
		    dst_y,
		    planar,
		    planar_rowb,
		    maskbm,
		    mask_rowb,
		    scratch_w,
		    scratch_h,
		    src_x,
		    src_y,
		    draw_w,
		    draw_h)) {
		return 0;
	}

	return (long)((size_t)draw_w * (size_t)draw_h);
}

/*
 * Take one rectangle of chunky (8-bit indexed) pixels and try to show it on the ST low-res planar
 * framebuffer using the BFTP path: “find or build a small hardware-friendly planar scratch, then
 * let the blitter copy it to the screen.” This function is the front door for that work for a
 * single rectangle (no tiling of oversized draws here).
 *
 * What it actually does:
 *   1) Look up by logical sprite identity (+ render variant tokens) across all tiers.
 *   2) On miss, decode once (optional lazy hook), scan minimal non-transparent crop, pick smallest
 *      tier pool whose per-slot planar+mask byte caps fit the crop's ST layout, fill slot, store meta.
 *   3) Blit only the intersection of current clip rectangle and cached crop rectangle.
 *
 * Return: blit_w*blit_h if the blitter path reports success, else 0 (skip, bad blit, etc.).
 *
 * Parameters:
 *   full_w/full_h — Decoded chunky frame extents at raster_base (stride src_stride ≥ full_w).
 *   Visible region: raster_ox, raster_oy, blit_w, blit_h — clip affects blit intersection only.
 */
static long sprite_cache_planar_composite_impl(uint8_t *dst_root_fb,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int ax0,
	int ay0,
	int blit_w,
	int blit_h,
	int src_stride,
	int full_w,
	int full_h,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	const uint8_t *raster_base,
	int raster_ox,
	int raster_oy,
	long identity_key,
	SpriteCacheLazyGate *lazy_gate)
{
	const long acc = sprite_cache_cached_tile_dispatch(dst_root_fb,
	    dst_row_bytes,
	    dst_width_pixels,
	    dst_height_pixels,
	    ax0,
	    ay0,
	    blit_w,
	    blit_h,
	    src_stride,
	    full_w,
	    full_h,
	    trans,
	    ghost_tab,
	    fade_tab,
	    raster_base,
	    raster_ox,
	    raster_oy,
	    identity_key,
	    lazy_gate);
	return acc ? acc : 0;
}

long ST_SPRITE_CACHE_Buffer_Frame_Planar_Composite(uint8_t *dst_root_fb,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int ax0,
	int ay0,
	const uint8_t *src,
	int blit_w,
	int blit_h,
	int src_stride,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	const uint8_t *raster_base,
	int raster_ox,
	int raster_oy,
	int full_w,
	int full_h,
	long identity_key,
	unsigned long (*lazy_decode_miss)(void *user_ctx),
	void *lazy_decode_ctx)
{
	sprite_cache_maybe_init();
	if (!dst_root_fb || dst_row_bytes <= 0 || dst_width_pixels <= 0 || dst_height_pixels <= 0
		|| blit_w <= 0 || blit_h <= 0 || src_stride <= 0 || !raster_base) {
		return 0;
	}
	if (full_w <= 0 || full_h <= 0 || full_w > src_stride) {
		return 0;
	}
	if (raster_ox < 0 || raster_oy < 0) {
		return 0;
	}
	if (raster_ox + blit_w > full_w || raster_oy + blit_h > full_h) {
		return 0;
	}
	if (!g_sprite_cache_slab) {
		return 0;
	}
	{
		const uint8_t *const expect =
		    raster_base + (size_t)raster_oy * (size_t)src_stride + (size_t)raster_ox;
		if (src != expect) {
			return 0;
		}
	}

	SpriteCacheLazyGate gate_stack;
	SpriteCacheLazyGate *gate_ptr = NULL;
	if (lazy_decode_miss != nullptr) {
		gate_stack.fill = lazy_decode_miss;
		gate_stack.ctx = lazy_decode_ctx;
		gate_stack.decoded = 0;
		gate_ptr = &gate_stack;
	}

	return sprite_cache_planar_composite_impl(dst_root_fb,
	    dst_row_bytes,
	    dst_width_pixels,
	    dst_height_pixels,
	    ax0,
	    ay0,
	    blit_w,
	    blit_h,
	    src_stride,
	    full_w,
	    full_h,
	    trans,
	    ghost_tab,
	    fade_tab,
	    raster_base,
	    raster_ox,
	    raster_oy,
	    identity_key,
	    gate_ptr);
}
