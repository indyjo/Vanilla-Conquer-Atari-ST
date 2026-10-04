/*
 * st_sprite_cache.h — Ranked ring planar sprite cache for Buffer_Frame_To_Page (Atari ST).
 */

#ifndef ATARILIB_ST_SPRITE_CACHE_H_
#define ATARILIB_ST_SPRITE_CACHE_H_

#include <stdint.h>

#ifdef __cplusplus
#include "st_decode_context.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void ST_SPRITE_CACHE_Init(void);

/* Drop all cached planar slots (e.g. after C2P weight-set install). */
void ST_SPRITE_CACHE_Invalidate_Planar_Cache(void);

/*
 * Purge cached slots and repartition the resident slab for the given tier slot counts.
 * The slab is Alloc'd once (default capacity bytes) and never freed; a layout that needs
 * more bytes than that slab returns -1 without changing the current configuration.
 * Pool indices 0..3 are named after 16² / 32² / 64² / 96² reference squares used only to
 * set each tier's per-slot byte budget (planar+mask); crops of any shape fit if packed
 * size ≤ that budget. The cache picks the smallest such pool. Counts are rounded down to
 * whole shards. Any count may be 0 to disable that tier; at least one must be non-zero.
 * Returns 0 on success, -1 if invalid or the layout does not fit the slab.
 */
int ST_SPRITE_CACHE_Reconfigure_TierCapacities(int cap_16, int cap_32, int cap_64, int cap_96);

/* Restore compile-time default tier capacities (purges; keeps the resident slab). */
void ST_SPRITE_CACHE_Reset_Tier_Capacities_To_Defaults(void);

/* Alt+D: print per-tier sprite cache stats to stdout, then reset counters. */
void ST_Sprite_Cache_Stats_Debug_Service(void);

#ifdef __cplusplus
} /* extern "C" */

/*
 * One-shot decode gate for a cache miss. fill runs at most once per call and must return
 * the same pointer as SpriteCacheBlit::raster. It may set_clip_bounds() to skip a raster scan.
 */
struct SpriteCacheLazyGate {
	unsigned long (*fill)(void *ctx, IDecodeContext *decode_ctx);
	void *ctx;
	ClipBounds clip_bounds;
	unsigned char decoded; /* 1 after first successful fill */
};

/*
 * One sprite composite. The visible chunky byte at (ox, oy) is raster + oy * stride + ox;
 * callers do not pass that pointer separately.
 * gate is null when there is no miss callback. Opaque draws (trans == 0) probe one tier;
 * transparent draws walk tiers.
 */
struct SpriteCacheBlit {
	uint8_t *dst;
	int dst_bpl;
	int dst_w;
	int dst_h;
	int dx;
	int dy;
	int blit_w;
	int blit_h;
	const uint8_t *raster;
	int stride;
	int full_w;
	int full_h;
	int ox;
	int oy;
	int trans;
	const uint8_t *ghost;
	const uint8_t *fade;
	const void *identity;
	int frame;
	SpriteCacheLazyGate *gate;
};

/*
 * Rasterize decoded shape bytes into a ranked-ring pool planar scratch and composite with the blitter.
 * Ghost approximates translucent drawing via checkerboard mask dither (fully cacheable).
 *
 * Return value: pixels composited (>= 0), including 0 when the viewport clip does not intersect the
 * cached crop. Returns -1 on hard failure (tier/blit/decode).
 */
long ST_SPRITE_CACHE_Buffer_Frame_Planar_Composite(SpriteCacheBlit const *req);

#endif /* __cplusplus */

#endif /* ATARILIB_ST_SPRITE_CACHE_H_ */
