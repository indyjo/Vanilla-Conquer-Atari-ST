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

typedef struct __attribute__((packed)) ST_Blitter {
	uint16_t src_x_inc;
	uint16_t src_y_inc;
	void *src_addr;
	uint16_t endmask1;
	uint16_t endmask2;
	uint16_t endmask3;
	uint16_t dst_x_inc;
	uint16_t dst_y_inc;
	void *dst_addr;
	uint16_t x_count;
	uint16_t y_count;
	uint8_t hop;
	uint8_t op;
	uint8_t ctrl;
	uint8_t skew;
} ST_Blitter;

void ST_Blitter_Await(void);

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
 * Planar rectangle copy (4 interleaved bitplanes, 8 bytes per 16 pixels).
 * Caller must clip to buffer bounds before calling.
 * Non-overlap blits up to 32×32 use HOG mode automatically; overlap (scroll) blits
 * stay cooperative regardless of size.
 */
BOOL ST_Blitter_Planar_Rect_Blit(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

/* Same layout; destination merge (OP = D|S) instead of replace. */
BOOL ST_Blitter_Planar_Rect_Blit_Or(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

/**
 * AND a 1bpp mask into all four planes of an ST planar destination.
 * Mask bit 1 preserves the destination pixel; mask bit 0 clears it to black.
 */
BOOL ST_Blitter_Mask_And_Planar_Rect(
	const uint8_t *mask_root,
	int mask_row_bytes,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

#ifdef __cplusplus
}
#endif

#endif /* ST_BLITTER_BLIT_H */
