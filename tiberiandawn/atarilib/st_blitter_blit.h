/*
 * st_blitter_blit.h - ST hardware blitter planar rectangle copy (scroll / self-blit).
 * Used by drawbuff Linear_Blit_To_Linear and by the on-machine test suite.
 */

#ifndef ST_BLITTER_BLIT_H
#define ST_BLITTER_BLIT_H

#include "c2p.h"
#include "function.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copy a rectangle in the ST low-res planar framebuffer using the blitter (all four planes).
 * Typical use: same buffer for src and dst (overlap-safe scroll). Coordinates are absolute
 * pixel coordinates (0..319, 0..199). Returns TRUE if the blit ran, FALSE if unsupported or invalid.
 */
BOOL ST_Blitter_Planar_Screen_Rect_Blit(
	const uint8_t *src_root,
	uint8_t *dst_root,
	int sx_abs,
	int sy_abs,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height);

/**
 * Generic planar rectangle copy using ST blitter.
 * Layout is Atari 4-plane interleaved (8 bytes per 16 pixels).
 * Coordinates are in pixels relative to each surface.
 */
BOOL ST_Blitter_Planar_Rect_Blit(
	const uint8_t *src_root,
	int src_row_bytes,
	int src_width_pixels,
	int src_height_pixels,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

#ifdef __cplusplus
}
#endif

#endif /* ST_BLITTER_BLIT_H */
