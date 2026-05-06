/*
 * st_sprite_cache.h — LRU-backed planar sprite cache for Buffer_Frame_To_Page (Atari ST).
 */

#ifndef ATARILIB_ST_SPRITE_CACHE_H_
#define ATARILIB_ST_SPRITE_CACHE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rasterize decoded shape bytes into LRU pool texture + composite with blitter.
 * Ghost approximates translucent drawing via checkerboard mask dither (fully cacheable).
 *
 * Blits requiring a tier larger than 96 (sprite max side after 16-px width roundup) return 0.
 *
 * LRU keys mix identity_key with render-variant fingerprints (fade/ghost/trans state).
 * Cache slots store a cropped (minimal non-transparent) representation plus insets; viewport clip
 * (raster_ox/oy, blit_w/h) is not part of the key. Each draw intersects the requested clip with the
 * cached crop and adjusts source/destination offsets so output matches the uncropped result.
 *
 * full_w/full_h — unclipped frame width/height (same as logical stride rows / Buffer_Frame_To_Page w,h).
 * lazy_decode_miss: when non-NULL, invokes once on LRU cache miss — return must equal raster_base.
 */
long ST_SPRITE_CACHE_Buffer_Frame_Planar_Composite(uint8_t *dst_root,
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
	unsigned long (*lazy_decode_miss)(void *user_ctx),
	void *lazy_decode_ctx);

void ST_SPRITE_CACHE_Init(void);

/*
 * Opaque identity for Bftp_ExArgs.identity_key: fingerprints shape/icon blob root + frame index
 * so planar LRU rows do not alias different tiles that share clip geometry (e.g. map stamps).
 */
long ST_SPRITE_CACHE_Frame_Identity_Key(void const *blobs_root, int frame_index);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ATARILIB_ST_SPRITE_CACHE_H_ */
