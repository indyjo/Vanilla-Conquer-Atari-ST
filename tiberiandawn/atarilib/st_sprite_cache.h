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
 * Purge the sprite cache and rebuild tier slot pools with the given slot counts. Pool indices 0..3
 * are named after 16² / 32² / 64² / 96² reference squares used only to set each tier's per-slot
 * byte budget (planar+mask); crops of any shape fit if packed size ≤ that budget. The cache picks
 * the smallest such pool. Counts are rounded down to whole shards of 8 slots. Any count may be 0 to
 * disable that tier; at least one must be non-zero. Returns 0 on success, -1 if invalid or alloc fails.
 */
int ST_SPRITE_CACHE_Reconfigure_TierCapacities(int cap_16, int cap_32, int cap_64, int cap_96);

/* Restore compile-time default tier capacities (purges the cache). */
void ST_SPRITE_CACHE_Reset_Tier_Capacities_To_Defaults(void);

/*
 * Opaque identity for Bftp_ExArgs.identity_key: fingerprints shape/icon blob root + frame index
 * so planar cache rows do not alias different tiles that share clip geometry (e.g. map stamps).
 */
long ST_SPRITE_CACHE_Frame_Identity_Key(void const *blobs_root, int frame_index);

/* Alt+D: print per-tier sprite cache stats to stdout, then reset counters. */
void ST_Sprite_Cache_Stats_Debug_Service(void);

#ifdef __cplusplus
} /* extern "C" */

/*
 * Rasterize decoded shape bytes into a ranked-ring pool planar scratch + composite with blitter.
 * Ghost approximates translucent drawing via checkerboard mask dither (fully cacheable).
 *
 * lazy_decode_miss: optional; runs at most once per call on cache miss — return must equal raster_base.
 *   The callback may call set_clip_bounds() on the supplied IDecodeContext to skip a raster scan.
 * Opaque draws (trans == 0) probe only the tier that fits the full frame; transparent draws walk tiers.
 *
 * Return value: pixels composited (>= 0), including 0 when the viewport clip does not intersect the
 * cached crop. Returns -1 on hard failure (tier/blit/decode).
 */
long ST_SPRITE_CACHE_Buffer_Frame_Planar_Composite(uint8_t *dst_root,
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
	const uint8_t *ghost_table,
	const uint8_t *fade_table,
	const uint8_t *raster_base,
	int raster_ox,
	int raster_oy,
	int full_w,
	int full_h,
	long identity_key,
	unsigned long (*lazy_decode_miss)(void *user_ctx, IDecodeContext *decode_ctx),
	void *lazy_decode_ctx);

#endif /* __cplusplus */

#endif /* ATARILIB_ST_SPRITE_CACHE_H_ */
