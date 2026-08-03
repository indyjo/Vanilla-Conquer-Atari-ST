/*
 * Ranked ring-cached planar + 1bpp mask sprite cache for Buffer_Frame_To_Page (Atari ST).
 *
 * Each RankCache directory node is (hash32, SpriteSlotHeader*). The slab slot layout is
 * [header: full key + crop/mask_off | planar | 1bpp mask]. Key stores raw shape_id/frame (and
 * remap table addresses); directory hash is a cheap xor/shift fold of those fields, then the
 * header key is verified on match. Bubble/swaps only touch the tiny directory entries.
 */

#include "st_sprite_cache.h"

#include "st_decode_context.h"

#include "c2p.h"
#include "memflag.h"
#include "rankcache.h"
#include "st_blit.h"
#include "st_frame_meter.h"

#include "ikbd.h"
#include "keyboard.h"

#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

/*
 * Each tier is a set of independent RankCache rings ("shards") of fixed length.
 * Defaults: 16/4/2/1 shards × 8 slots → capacities 128/32/16/8 (dims 16/32/64/96).
 * Shard index comes only from shape address + frame, never remap/fade/ghost.
 */
#ifndef ST_SPRITE_CACHE_SHARD_SIZE
#define ST_SPRITE_CACHE_SHARD_SIZE 8
#endif
#ifndef ST_SPRITE_CACHE_SHARDS_16
#define ST_SPRITE_CACHE_SHARDS_16 16
#endif
#ifndef ST_SPRITE_CACHE_SHARDS_32
#define ST_SPRITE_CACHE_SHARDS_32 4
#endif
#ifndef ST_SPRITE_CACHE_SHARDS_64
#define ST_SPRITE_CACHE_SHARDS_64 2
#endif
#ifndef ST_SPRITE_CACHE_SHARDS_96
#define ST_SPRITE_CACHE_SHARDS_96 1
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_16
#define ST_SPRITE_CACHE_CAPACITY_16 (ST_SPRITE_CACHE_SHARDS_16 * ST_SPRITE_CACHE_SHARD_SIZE)
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_32
#define ST_SPRITE_CACHE_CAPACITY_32 (ST_SPRITE_CACHE_SHARDS_32 * ST_SPRITE_CACHE_SHARD_SIZE)
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_64
#define ST_SPRITE_CACHE_CAPACITY_64 (ST_SPRITE_CACHE_SHARDS_64 * ST_SPRITE_CACHE_SHARD_SIZE)
#endif
#ifndef ST_SPRITE_CACHE_CAPACITY_96
#define ST_SPRITE_CACHE_CAPACITY_96 (ST_SPRITE_CACHE_SHARDS_96 * ST_SPRITE_CACHE_SHARD_SIZE)
#endif
/* Hard clamp for per-tier slot count (RankCache uses uint16_t ranks). */
#ifndef ST_SPRITE_CACHE_TIER_CAP_MAX
#define ST_SPRITE_CACHE_TIER_CAP_MAX 4096
#endif
/* Reference square sides that define each tier's per-slot byte budget (not max W/H). */
enum {
	SPRITE_CACHE_D16 = 16,
	SPRITE_CACHE_D32 = 32,
	SPRITE_CACHE_D64 = 64,
	SPRITE_CACHE_D96 = 96,
	SPRITE_CACHE_TIER_COUNT = 4
};
/* Stack row buffer for remap-then-bulk-C2P; covers wide crops with headroom. */
enum { SPRITE_CACHE_ROW_BUF_MAX = 320 };

/*
 * One-shot decode gate for miss-time lazy frame build.
 */
struct SpriteCacheLazyGate {
	unsigned long (*fill)(void *ctx, IDecodeContext *decode_ctx);
	void *ctx;
	ClipBounds clip_bounds;
	unsigned char decoded; /* 1 after first successful fill */
};

/*
 * UnitShadow ghost rows (see DISPLAY.CPP Conquer_Build_Translucent_Table) fade LTGREEN→BLACK against
 * a logical backdrop palette index; without reading the framebuffer, use BLACK (EGA logical 12).
 */
enum { SPRITE_CACHE_GHOST_SYNTH_BACKDROP_IX = 12 };

/* Logical cache key: raw identity components plus render-variant discriminators. */
struct SpriteCacheKey {
	uint32_t shape_id; /* (uint32_t)(uintptr_t) blob root — not hashed */
	uint16_t frame;
	uint8_t mode_pack; /* 0 plain, 1 fade, 2 ghost */
	uint8_t trans_flag; /* 0/1 */
	uint32_t fade_id; /* 0 none; else (uint32_t)(uintptr_t) fade table */
	uint32_t ghost_id; /* 0 none; else (uint32_t)(uintptr_t) ghost table */

	bool operator==(const SpriteCacheKey &o) const
	{
		return shape_id == o.shape_id && frame == o.frame && mode_pack == o.mode_pack
		       && trans_flag == o.trans_flag && fade_id == o.fade_id && ghost_id == o.ghost_id;
	}
};

/*
 * Per-slot slab header + payload:
 *   [ SpriteSlotHeader | planar pixels | 1bpp mask ]
 * RankCache directory stores only hash32 + pointer to this header.
 * Vacant / never-filled slots keep crop_w = crop_h = 0.
 */
struct SpriteSlotHeader {
	SpriteCacheKey key;
	uint16_t mask_off; /* planar byte count for this fill; mask follows planar */
	uint16_t crop_x;
	uint16_t crop_y;
	uint16_t crop_w;
	uint16_t crop_h;
};

/* Directory: hashed key + slab pointer (full key lives in the header). */
using SpriteRankCache = RankCache<uint32_t, SpriteSlotHeader *>;

/* Word-align header so planar/mask rows stay even-addressed on 68000. */
static inline size_t sprite_slot_hdr_bytes(void)
{
	return (sizeof(SpriteSlotHeader) + 1u) & ~1u;
}

static inline uint8_t *sprite_slot_planar(SpriteSlotHeader const *h)
{
	return (uint8_t *)(void *)h + sprite_slot_hdr_bytes();
}

static inline uint8_t *sprite_slot_mask(SpriteSlotHeader const *h)
{
	return sprite_slot_planar(h) + (size_t)h->mask_off;
}

/*
 * Cheap directory hash for 68000: shifts/adds/xors only (no multiplies, no avalanche chain).
 * Collision rate only needs to be tolerable inside an 8-entry shard (full key still verified).
 */
static inline uint32_t sprite_cache_key_hash(SpriteCacheKey const &k)
{
	uint32_t h = k.shape_id;
	h += (uint32_t)k.frame;
	h ^= (uint32_t)k.frame << 11;
	h += ((uint32_t)k.mode_pack << 1) | (uint32_t)k.trans_flag;
	h ^= k.fade_id;
	h ^= k.ghost_id << 1;
	return h;
}

/* Predicates for hash-directory lookup (reject collisions via full key in header). */
struct SpriteSlotKeyMatch {
	SpriteCacheKey const *want;
	bool operator()(SpriteSlotHeader *h) const
	{
		return h != nullptr && h->key == *want;
	}
};

/* One configured tier pool (fixed byte-capacity slots + sharded ranked rings). */
struct SpriteCacheTier {
	/*
	 * Reference square side used only to size payload_sz (bytes for planar+mask of a dim×dim
	 * sprite). Not a max width/height — long/thin crops are fine if need_p+need_m ≤ payload_sz.
	 */
	int dim = 0;
	int capacity = 0; /* total slots across all shards */
	int shard_count = 0; /* independent RankCache rings */
	int shard_size = 0; /* slots per shard (normally ST_SPRITE_CACHE_SHARD_SIZE) */
	uint8_t *slot_base = nullptr; /* capacity × slot_sz */
	int payload_sz = 0; /* max packed planar+mask bytes per slot */
	int slot_sz = 0; /* header + payload_sz */
	SpriteRankCache **shards = nullptr; /* [shard_count] */
};

/* Per-tier runtime counters used by Alt+D stats dump. */
struct SpriteCacheTierStats {
	unsigned long hits;
	unsigned long misses;
	unsigned long evictions;
	unsigned long fills_fast;
	unsigned long fills_slow;
	unsigned long filled_pixels_sum;
};

static SpriteCacheTier g_sprite_cache_tiers[SPRITE_CACHE_TIER_COUNT];
static uint8_t *g_sprite_cache_slab = nullptr;
static bool g_sprite_cache_inited = false;
static int g_sprite_cache_cap[SPRITE_CACHE_TIER_COUNT] = {
	ST_SPRITE_CACHE_CAPACITY_16,
	ST_SPRITE_CACHE_CAPACITY_32,
	ST_SPRITE_CACHE_CAPACITY_64,
	ST_SPRITE_CACHE_CAPACITY_96
};
static SpriteCacheTierStats g_sprite_cache_stats[SPRITE_CACHE_TIER_COUNT];
static bool g_sprite_cache_stats_key_prev = false;

static const int g_sprite_cache_dims[SPRITE_CACHE_TIER_COUNT] = {
	SPRITE_CACHE_D16, SPRITE_CACHE_D32, SPRITE_CACHE_D64, SPRITE_CACHE_D96
};

/*
 * Pick shard from shape address + frame only.
 * Remap / fade / ghost / trans must not affect this — variants of one sprite share a shard.
 */
static inline unsigned sprite_cache_shard_index(uint32_t shape_id, uint16_t frame, int shard_count)
{
	if (shard_count <= 1)
		return 0u;
	const uint32_t bits = shape_id ^ (uint32_t)frame;
	/* Default shard counts are powers of two (16/4/2/1). */
	if ((shard_count & (shard_count - 1)) == 0)
		return (unsigned)bits & (unsigned)(shard_count - 1);
	return (unsigned)(bits % (uint32_t)shard_count);
}

/*
 * Flush up to 16 MSB-first mask bits into one 16-pixel mask word (2 bytes).
 * Unused low bits are filled with 1 (preserve backdrop). Matches blitter mask layout:
 * one bit per pixel, MSB = leftmost pixel in the 16-column group.
 */
static inline void sprite_cache_mask_flush_run(uint8_t *mask_row, int word_ix, int cols, uint16_t accum)
{
	if (cols <= 0)
		return;
	const uint16_t word = (cols >= 16)
	    ? accum
	    : (uint16_t)(((uint16_t)accum << (16 - cols)) | (uint16_t)(0xFFFFu >> cols));
	*(uint16_t *)(mask_row + word_ix * 2) = word;
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

/* Pick smallest tier whose per-slot byte cap fits the crop's packed planar+mask size. */
static int sprite_cache_pick_tier_index_for_crop(int crop_w, int crop_h)
{
	if (crop_w < 0 || crop_h < 0)
		return -1;
	if (crop_w == 0 || crop_h == 0) {
		/* Fully transparent: use smallest enabled pool so retarget still has a slot. */
		int best_i = -1;
		int best_sz = 0;
		for (int i = 0; i < SPRITE_CACHE_TIER_COUNT; ++i) {
			SpriteCacheTier *tr = &g_sprite_cache_tiers[i];
			if (tr->capacity <= 0)
				continue;
			if (best_i < 0 || tr->payload_sz < best_sz) {
				best_i = (int)i;
				best_sz = tr->payload_sz;
			}
		}
		return best_i;
	}

	size_t need_p = 0;
	size_t need_m = 0;
	sprite_cache_layout_for_crop(crop_w, crop_h, nullptr, nullptr, nullptr, nullptr, &need_p, &need_m);
	const size_t need = need_p + need_m;

	int best_i = -1;
	int best_sz = 0;
	for (int i = 0; i < SPRITE_CACHE_TIER_COUNT; ++i) {
		SpriteCacheTier *tr = &g_sprite_cache_tiers[i];
		if (tr->capacity <= 0)
			continue;
		if (need > (size_t)tr->payload_sz)
			continue;
		if (best_i < 0 || tr->payload_sz < best_sz) {
			best_i = (int)i;
			best_sz = tr->payload_sz;
		}
	}
	return best_i;
}

static void sprite_cache_reset_stats(void)
{
	std::memset(g_sprite_cache_stats, 0, sizeof(g_sprite_cache_stats));
}

#if defined(__MINT__)
static void sprite_cache_dump_stats_and_reset(void)
{
	for (int ti = 0; ti < SPRITE_CACHE_TIER_COUNT; ++ti) {
		const SpriteCacheTierStats &st = g_sprite_cache_stats[ti];
		unsigned long fill_ratio_100 = 0;
		unsigned long cached_slots = 0;
		unsigned long cached_pixels_sum = 0;
		SpriteCacheTier *const tr = &g_sprite_cache_tiers[ti];
		if (tr->shards) {
			for (int sh = 0; sh < tr->shard_count; ++sh) {
				SpriteRankCache *rank = tr->shards[sh];
				if (!rank || !rank->nodes())
					continue;
				SpriteRankCache::Node const *nodes = rank->nodes();
				const int n = (int)rank->capacity();
				for (int si = 0; si < n; ++si) {
					SpriteSlotHeader const *sm = nodes[si].value;
					if (!sm || (sm->crop_w == 0 && sm->crop_h == 0))
						continue;
					cached_slots++;
					cached_pixels_sum += (unsigned long)sm->crop_w * (unsigned long)sm->crop_h;
				}
			}
		}
		if (cached_slots != 0UL) {
			const unsigned long slot_pixels =
			    (unsigned long)g_sprite_cache_dims[ti] * (unsigned long)g_sprite_cache_dims[ti];
			const unsigned long denom = cached_slots * slot_pixels;
			if (denom != 0UL) {
				fill_ratio_100 = (cached_pixels_sum * 100UL + (denom / 2UL)) / denom;
			}
		}
		const unsigned long fills = st.fills_fast + st.fills_slow;
		printf("SpriteCache tier %d: hits=%lu misses=%lu fills=%lu avg_fill=%lu%%\n",
		    g_sprite_cache_dims[ti],
		    st.hits,
		    st.misses,
		    fills,
		    fill_ratio_100);
	}
	std::fflush(stdout);
	sprite_cache_reset_stats();
}
#endif

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

static void sprite_cache_reseed_tier_rank(SpriteCacheTier &tr, int tier_index)
{
	if (!tr.shards || tr.shard_count <= 0 || tr.capacity <= 0 || !tr.slot_base)
		return;

	uint16_t global_i = 0;
	for (int sh = 0; sh < tr.shard_count; ++sh) {
		SpriteRankCache *rank = tr.shards[sh];
		if (!rank || !rank->valid())
			continue;
		rank->reset_head();
		const uint16_t n = rank->capacity();
		for (uint16_t i = 0; i < n; ++i, ++global_i) {
			SpriteCacheKey dk;
			std::memset(&dk, 0, sizeof(dk));
			dk.shape_id = UINT32_MAX ^ ((((uint32_t)tier_index) << 20) ^ ((uint32_t)sh << 12) ^ (uint32_t)i);
			dk.frame = (uint16_t)(0xF000u + global_i);
			dk.mode_pack = 0xFF;
			dk.trans_flag = 0;
			dk.fade_id = (uint32_t)tier_index + 1u;
			dk.ghost_id = 0x1000u + (uint32_t)global_i;

			SpriteSlotHeader *hdr =
			    (SpriteSlotHeader *)(tr.slot_base + (size_t)global_i * (size_t)tr.slot_sz);
			hdr->key = dk;
			hdr->mask_off = 0;
			hdr->crop_x = 0;
			hdr->crop_y = 0;
			hdr->crop_w = 0;
			hdr->crop_h = 0;
			rank->set(i, sprite_cache_key_hash(dk), hdr);
		}
	}
}

void ST_SPRITE_CACHE_Init(void)
{
	sprite_cache_maybe_init();
}

extern "C" void ST_SPRITE_CACHE_Invalidate_Planar_Cache(void)
{
	if (!g_sprite_cache_inited)
		return;
	for (int t = 0; t < SPRITE_CACHE_TIER_COUNT; ++t)
		sprite_cache_reseed_tier_rank(g_sprite_cache_tiers[t], t);
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
	for (int t = 0; t < SPRITE_CACHE_TIER_COUNT; ++t) {
		SpriteCacheTier &tr = g_sprite_cache_tiers[t];
		if (tr.shards) {
			for (int sh = 0; sh < tr.shard_count; ++sh)
				delete tr.shards[sh];
			delete[] tr.shards;
			tr.shards = nullptr;
		}
		tr.slot_base = nullptr;
		tr.capacity = 0;
		tr.shard_count = 0;
		tr.shard_size = 0;
		tr.dim = 0;
		tr.payload_sz = 0;
		tr.slot_sz = 0;
	}
	Free(g_sprite_cache_slab);
	g_sprite_cache_slab = nullptr;
	g_sprite_cache_inited = false;
	sprite_cache_reset_stats();
}

static void sprite_cache_maybe_init(void)
{
	if (g_sprite_cache_inited)
		return;

	int payload_sz[SPRITE_CACHE_TIER_COUNT];
	int slot_sz[SPRITE_CACHE_TIER_COUNT];
	size_t total = 0;
	const size_t hdr_bytes = sprite_slot_hdr_bytes();

	for (int t = 0; t < SPRITE_CACHE_TIER_COUNT; ++t) {
		const int d = g_sprite_cache_dims[t];
		/* Byte budget from a d×d reference square (not a max width/height). */
		const int planar_bpl = ((d + 15) >> 4) * 8;
		const int mask_bpl = ((d + 15) >> 4) * 2;
		payload_sz[t] = planar_bpl * d + mask_bpl * d;
		slot_sz[t] = (int)hdr_bytes + payload_sz[t];
		const int cap = g_sprite_cache_cap[t];
		if (cap > 0)
			total += (size_t)cap * (size_t)slot_sz[t];
	}

	if (total == 0)
		return;

	g_sprite_cache_slab = (uint8_t *)Alloc((unsigned long)total, MEM_NORMAL);
	if (!g_sprite_cache_slab) {
		/* Alloc already invoked Memory_Error; do not continue without a slab. */
		return;
	}

	uint8_t *walk = g_sprite_cache_slab;
	for (int t = 0; t < SPRITE_CACHE_TIER_COUNT; ++t) {
		SpriteCacheTier &tr = g_sprite_cache_tiers[t];
		tr.dim = g_sprite_cache_dims[t];
		tr.capacity = g_sprite_cache_cap[t];
		tr.payload_sz = payload_sz[t];
		tr.slot_sz = slot_sz[t];
		tr.shards = nullptr;
		tr.shard_count = 0;
		tr.shard_size = 0;

		if (tr.capacity <= 0) {
			tr.slot_base = nullptr;
			continue;
		}

		/* Capacity is always a multiple of SHARD_SIZE after reconfigure/defaults. */
		tr.shard_size = ST_SPRITE_CACHE_SHARD_SIZE;
		tr.shard_count = tr.capacity / tr.shard_size;
		if (tr.shard_count <= 0) {
			tr.slot_base = nullptr;
			tr.capacity = 0;
			continue;
		}

		tr.slot_base = walk;
		walk += (size_t)tr.capacity * (size_t)tr.slot_sz;

		tr.shards = new (std::nothrow) SpriteRankCache *[tr.shard_count];
		if (!tr.shards)
			goto fail;
		for (int sh = 0; sh < tr.shard_count; ++sh)
			tr.shards[sh] = nullptr;

		for (int sh = 0; sh < tr.shard_count; ++sh) {
			tr.shards[sh] = new (std::nothrow) SpriteRankCache((uint16_t)tr.shard_size);
			if (!tr.shards[sh] || !tr.shards[sh]->valid())
				goto fail;
		}
		sprite_cache_reseed_tier_rank(tr, t);
	}

	g_sprite_cache_inited = true;
	return;
fail:
	sprite_cache_shutdown();
}

extern "C" int ST_SPRITE_CACHE_Reconfigure_TierCapacities(int c16, int c32, int c64, int c96)
{
	int caps[SPRITE_CACHE_TIER_COUNT] = { c16, c32, c64, c96 };
	for (int i = 0; i < SPRITE_CACHE_TIER_COUNT; ++i) {
		if (caps[i] < 0)
			return -1;
		if (caps[i] > ST_SPRITE_CACHE_TIER_CAP_MAX)
			caps[i] = ST_SPRITE_CACHE_TIER_CAP_MAX;
		/* Round down to whole shards of ST_SPRITE_CACHE_SHARD_SIZE. */
		if (caps[i] > 0)
			caps[i] = (caps[i] / ST_SPRITE_CACHE_SHARD_SIZE) * ST_SPRITE_CACHE_SHARD_SIZE;
	}
	int sum = 0;
	for (int i = 0; i < SPRITE_CACHE_TIER_COUNT; ++i)
		sum += caps[i];
	if (sum <= 0)
		return -1;

	sprite_cache_shutdown();
	for (int i = 0; i < SPRITE_CACHE_TIER_COUNT; ++i)
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

void ST_Sprite_Cache_Stats_Debug_Service(void)
{
#if defined(__MINT__)
	const bool down = (IKBD_Key_Is_Down(VK_MENU) && IKBD_Key_Is_Down(VK_D)) ? true : false;
	if (down && !g_sprite_cache_stats_key_prev) {
		sprite_cache_maybe_init();
		sprite_cache_dump_stats_and_reset();
	}
	g_sprite_cache_stats_key_prev = down;
#else
	(void)0;
#endif
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
		/* One destination pass in software; still two passes on the BLiTTER. */
		return ST_Blit_Mask_Merge_Planar_Rect(
			maskbm,
			mask_rowb,
			planar,
			planar_rowb,
			sx_abs,
			sy_abs,
			dst_root_fb,
			dst_row_bytes,
			dx_abs,
			dy_abs,
			blit_w,
			blit_h);
	}
	return ST_Blit_Planar_Rect_Blit(planar,
		   planar_rowb,
		   sx_abs,
		   sy_abs,
		   dst_root_fb,
		   dst_row_bytes,
		   dx_abs,
		   dy_abs,
		   blit_w,
		   blit_h)
		       ? TRUE
		       : FALSE;
}

/* Fills one cache slot from cropped source pixels and optional remaps.
 * Packs [header | planar | mask] tightly: mask begins at planar + need_p.
 * Returns 1 = fast path (bulk C2P), 2 = slow path (row remap + bulk line C2P), 0 = failure. */
static int sprite_cache_fill_slot_pixels(
	SpriteCacheTier *tr,
	SpriteSlotHeader *slot,
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
	if (crop_w <= 0 || crop_h <= 0) {
		slot->mask_off = 0;
		return 1;
	}
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
	if (scratch_w <= 0 || scratch_h <= 0) {
		slot->mask_off = 0;
		return 1;
	}
	if (need_p + need_m > (size_t)tr->payload_sz)
		return 0;
	if (need_p > 0xffffu)
		return 0;

	/* Tight pack: mask immediately follows the used planar bytes. */
	slot->mask_off = (uint16_t)need_p;
	uint8_t *planar = sprite_slot_planar(slot);
	uint8_t *maskbm = sprite_slot_mask(slot);
	std::memset(planar, 0, need_p);
	std::memset(maskbm, 0xFF, need_m);

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

	if (crop_w > SPRITE_CACHE_ROW_BUF_MAX)
		return 0;

	/*
	 * Remap path (trans / fade / ghost): per-row chunky buffer, then bulk line C2P.
	 * Per-pixel C2P_Map8ToPlanar4 + scratch writes was the main cache-miss hotspot; we
	 * still apply trans/fade/ghost per column but convert each row with PairLUT/movep.
	 * Mask bits are shift-accumulated 16 at a time instead of patching one byte per pixel.
	 * Backdrop columns use pal8=0 before C2P so planar stays color-0 for mask+OR blits.
	 */
	const BOOL masked_merge = (ghost_tab != nullptr) || (trans != 0);
	const uint8_t *const ghost_cls = ghost_tab;
	const uint8_t *const ghost_blend = ghost_cls ? ghost_cls + 256 : nullptr;
	uint8_t row_buf[SPRITE_CACHE_ROW_BUF_MAX];

	ST_FRAME_BAR_C2P_BEGIN();
	for (int row = crop_y; row < crop_y + crop_h; ++row) {
		const uint8_t *srow = src + (size_t)row * (size_t)stride;
		const int sy = row - crop_y;
		uint8_t *mask_row = maskbm + (size_t)sy * (size_t)mask_rowb;
		/* 16-bit left-shift accum for one mask word (16 pixels). */
		uint16_t mask_acc = 0;
		int mask_run = 0;
		int mask_word_ix = 0;

		/* Pass 1: remap source indices into row_buf; build mask in the same scan. */
		for (int sx = 0; sx < crop_w; ++sx) {
			const int col = crop_x + sx;
			const uint8_t raw = srow[col];
			bool preserve = true;
			uint8_t pal8 = 0;

			if (trans && raw == 0) {
				/* Leave pal8=0; mask preserves backdrop. */
			} else {
				pal8 = raw;
				if (ghost_cls && ghost_blend) {
					const uint8_t cls = ghost_cls[raw];
					if (cls != 0xFFu) {
						/*
						 * Sprite-local checkerboard mask dither (~50%); must not use screen coords
						 * so cached fills are valid at any placement.
						 */
						if (((col ^ row) & 1) != 0) {
							/* Checkerboard skip: preserve backdrop (pal8 cleared below). */
						} else {
							pal8 = ghost_blend[(size_t)cls * 256u + SPRITE_CACHE_GHOST_SYNTH_BACKDROP_IX];
							preserve = false;
						}
					} else {
						pal8 = fade_tab ? fade_tab[raw] : raw;
						preserve = false;
					}
				} else if (fade_tab) {
					pal8 = fade_tab[raw];
					preserve = false;
				} else {
					preserve = false;
				}
			}

			if (preserve)
				pal8 = 0;
			row_buf[sx] = pal8;

			if (masked_merge) {
				/* 1 = preserve (skip blit), 0 = draw this column. Shift every column. */
				mask_acc = (uint16_t)((mask_acc << 1) | (preserve ? 1u : 0u));
				mask_run++;
				if (mask_run == 16) {
					sprite_cache_mask_flush_run(mask_row, mask_word_ix, 16, mask_acc);
					mask_acc = 0;
					mask_run = 0;
					mask_word_ix++;
				}
			}
		}

		if (masked_merge && mask_run > 0)
			sprite_cache_mask_flush_run(mask_row, mask_word_ix, mask_run, mask_acc);

		/* Pass 2: 8bpp row → interleaved planar (Bayer uses sprite-local sx,sy). */
		C2P_Render_Logical_Row_To_Planar(
		    row_buf,
		    crop_w,
		    planar + (size_t)sy * (size_t)planar_rowb,
		    planar_rowb,
		    scratch_w,
		    scratch_h,
		    0,
		    sy,
		    0,
		    sy);
	}
	ST_FRAME_BAR_C2P_END();
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
	void const *identity_root,
	int identity_frame,
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
	want.shape_id = (uint32_t)(uintptr_t)identity_root;
	want.frame = (identity_frame < 0) ? (uint16_t)0xFFFFu : (uint16_t)identity_frame;
	want.mode_pack = mode_pack;
	want.trans_flag = (uint8_t)(trans != 0);
	want.fade_id = (uint32_t)(uintptr_t)fade_tab;
	want.ghost_id = (uint32_t)(uintptr_t)ghost_tab;
	const uint32_t want_hash = sprite_cache_key_hash(want);
	const SpriteSlotKeyMatch key_match = { &want };

	SpriteSlotHeader *slot = nullptr;
	SpriteCacheTier *tr = nullptr;
	bool cache_hit = false;

	/*
	 * Opaque draws (!SHAPE_TRANS): crop is always the full frame, so tier is known without
	 * an external clip table. Transparent draws need lazy clip (miss) or a full tier walk.
	 */
	ClipBounds opaque_clip;
	ClipBounds const *clip = nullptr;
	if (trans == 0) {
		opaque_clip.set(0, 0, full_w, full_h);
		clip = &opaque_clip;
	}
	const bool have_clip = clip != nullptr && clip->valid;
	int known_tier = -1;
	if (have_clip) {
		known_tier = sprite_cache_pick_tier_index_for_crop(clip->w, clip->h);
		if (known_tier < 0)
			return -1;

		SpriteCacheTier *probe = &g_sprite_cache_tiers[known_tier];
		if (probe->shards && probe->shard_count > 0) {
			const unsigned sh = sprite_cache_shard_index(want.shape_id, want.frame, probe->shard_count);
			SpriteRankCache *rank = probe->shards[sh];
			if (rank && rank->get(want_hash, slot, key_match)) {
				g_sprite_cache_stats[known_tier].hits++;
				tr = probe;
				cache_hit = true;
			} else {
				g_sprite_cache_stats[known_tier].misses++;
			}
		}
	} else {
		/* Transparent and no clip bounds: fall back to probing every enabled tier. */
		for (int ti = 0; ti < SPRITE_CACHE_TIER_COUNT; ++ti) {
			SpriteCacheTier *probe = &g_sprite_cache_tiers[ti];
			if (!probe->shards || probe->shard_count <= 0)
				continue;
			const unsigned sh = sprite_cache_shard_index(want.shape_id, want.frame, probe->shard_count);
			SpriteRankCache *rank = probe->shards[sh];
			if (!rank)
				continue;
			if (rank->get(want_hash, slot, key_match)) {
				g_sprite_cache_stats[ti].hits++;
				tr = probe;
				cache_hit = true;
				break;
			}
			g_sprite_cache_stats[ti].misses++;
		}
	}

	if (!cache_hit) {
		if (lazy_gate != nullptr && lazy_gate->fill != nullptr && lazy_gate->decoded == 0) {
			lazy_gate->clip_bounds.reset();
			IDecodeContext decode_iface = IDecodeContext::bind(&lazy_gate->clip_bounds);
			unsigned long const built = lazy_gate->fill(lazy_gate->ctx, &decode_iface);
			if (built == 0UL) {
				return -1;
			}
			if ((const uint8_t *)(uintptr_t)built != raster_base) {
				return -1;
			}
			lazy_gate->decoded = 1;
		}

		int crop_x = 0;
		int crop_y = 0;
		int crop_w = full_w;
		int crop_h = full_h;
		if (have_clip) {
			crop_x = clip->x;
			crop_y = clip->y;
			crop_w = clip->w;
			crop_h = clip->h;
		} else if (lazy_gate != nullptr && lazy_gate->clip_bounds.valid) {
			crop_x = lazy_gate->clip_bounds.x;
			crop_y = lazy_gate->clip_bounds.y;
			crop_w = lazy_gate->clip_bounds.w;
			crop_h = lazy_gate->clip_bounds.h;
		} else {
			sprite_cache_scan_crop_bounds(
			    raster_base, full_w, full_h, src_stride, trans, &crop_x, &crop_y, &crop_w, &crop_h);
		}

		const int tier_ix = (known_tier >= 0) ? known_tier : sprite_cache_pick_tier_index_for_crop(crop_w, crop_h);
		if (tier_ix < 0)
			return -1;

		tr = &g_sprite_cache_tiers[tier_ix];
		if (!tr->shards || tr->shard_count <= 0) {
			return -1;
		}
		{
			const unsigned sh = sprite_cache_shard_index(want.shape_id, want.frame, tr->shard_count);
			SpriteRankCache *rank = tr->shards[sh];
			if (!rank || !rank->retarget_oldest(want_hash, slot, key_match) || slot == nullptr) {
				return -1;
			}
		}

		if (slot->crop_w != 0 || slot->crop_h != 0) {
			const int ti_stat = (int)(tr - g_sprite_cache_tiers);
			if (ti_stat >= 0 && ti_stat < SPRITE_CACHE_TIER_COUNT)
				g_sprite_cache_stats[ti_stat].evictions++;
		}

		slot->key = want;

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
			return -1;
		}

		slot->crop_x = (uint16_t)crop_x;
		slot->crop_y = (uint16_t)crop_y;
		slot->crop_w = (uint16_t)crop_w;
		slot->crop_h = (uint16_t)crop_h;
		for (int ti = 0; ti < SPRITE_CACHE_TIER_COUNT; ++ti) {
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

	if (!tr || !slot)
		return -1;

	const int req_x0 = clip_ox;
	const int req_y0 = clip_oy;
	const int req_x1 = clip_ox + clip_w;
	const int req_y1 = clip_oy + clip_h;
	const int crop_x0 = (int)slot->crop_x;
	const int crop_y0 = (int)slot->crop_y;
	const int crop_x1 = crop_x0 + (int)slot->crop_w;
	const int crop_y1 = crop_y0 + (int)slot->crop_h;

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

	const uint8_t *planar = sprite_slot_planar(slot);
	const uint8_t *maskbm = sprite_slot_mask(slot);

	int scratch_w = 0;
	int scratch_h = 0;
	int planar_rowb = 0;
	int mask_rowb = 0;
	sprite_cache_layout_for_crop(
	    (int)slot->crop_w, (int)slot->crop_h, &scratch_w, &scratch_h, &planar_rowb, &mask_rowb, nullptr, nullptr);

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
		return -1;
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
 *   1) Look up by logical sprite identity (+ render variant tokens) in one identity-hashed shard.
 *      Opaque full-frame probes one tier; transparent draws try all tiers.
 *   2) On miss, decode once (optional lazy hook), use lazy clip or scan crop, pick tier,
 *      fill that shard's LRU slot, store meta.
 *   3) Blit only the intersection of current clip rectangle and cached crop rectangle.
 *
 * Return: pixels composited (>= 0), or -1 on hard failure.
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
	void const *identity_root,
	int identity_frame,
	SpriteCacheLazyGate *lazy_gate)
{
	return sprite_cache_cached_tile_dispatch(dst_root_fb,
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
	    identity_root,
	    identity_frame,
	    lazy_gate);
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
	void const *identity_root,
	int identity_frame,
	unsigned long (*lazy_decode_miss)(void *user_ctx, IDecodeContext *decode_ctx),
	void *lazy_decode_ctx)
{
	sprite_cache_maybe_init();
	if (!dst_root_fb || dst_row_bytes <= 0 || dst_width_pixels <= 0 || dst_height_pixels <= 0
		|| blit_w <= 0 || blit_h <= 0 || src_stride <= 0 || !raster_base) {
		return -1;
	}
	if (full_w <= 0 || full_h <= 0 || full_w > src_stride) {
		return -1;
	}
	if (raster_ox < 0 || raster_oy < 0) {
		return -1;
	}
	if (raster_ox + blit_w > full_w || raster_oy + blit_h > full_h) {
		return -1;
	}
	if (!g_sprite_cache_slab) {
		return -1;
	}
	{
		const uint8_t *const expect =
		    raster_base + (size_t)raster_oy * (size_t)src_stride + (size_t)raster_ox;
		if (src != expect) {
			return -1;
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
	    identity_root,
	    identity_frame,
	    gate_ptr);
}
