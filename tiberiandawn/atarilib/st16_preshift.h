/*
 * st16_preshift.h - pre-shifted terrain tiles.
 *
 * A tile drawn at an arbitrary x needs its bits shifted across 16-pixel word
 * boundaries; that skew is about half the work in the blit inner loop. Terrain
 * tiles are static and finite, so the 16 possible shifts can be built once and
 * reused, leaving a skew-free blit.
 */

#ifndef ATARILIB_ST16_PRESHIFT_H_
#define ATARILIB_ST16_PRESHIFT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shift a 4-plane planar block right by `shift` pixels (0..15).
 *
 * src is src_w pixels wide with src_row_bytes per row; dst must hold
 * src_w + shift pixels per row at dst_row_bytes. Both use the ST layout: four
 * interleaved plane words, 8 bytes per 16 pixels. dst is fully written.
 */
void ST16_Preshift_Planar(const uint8_t *src,
	int src_row_bytes,
	int src_w,
	int rows,
	uint8_t *dst,
	int dst_row_bytes,
	int shift);

/*
 * Same for a 1bpp mask: one word per 16 pixels, 2 bytes per column. Vacated
 * bits are set, not cleared - a zero mask bit clears the destination pixel, and
 * the area outside the tile must stay untouched.
 */
void ST16_Preshift_Mask(const uint8_t *src,
	int src_row_bytes,
	int src_w,
	int rows,
	uint8_t *dst,
	int dst_row_bytes,
	int shift);

/*
 * Cache of pre-shifted 24x24 tiles, keyed by the tile's planar address and the
 * shift. Only that size is cached - it is the terrain template case and keeps
 * the slots uniform; anything else takes the normal skewed blit.
 */
typedef struct ST16_PreshiftView {
	const uint8_t *planar;
	const uint8_t *mask; /* NULL when the tile has none */
	int planar_row_bytes;
	int mask_row_bytes;
	int src_x; /* where the tile starts inside the block: equals the shift */
} ST16_PreshiftView;

/*
 * Fetch (building it on first use) the variant of `planar` shifted by `shift`.
 * Returns 0 when the tile is not cacheable or no memory is available, in which
 * case the caller must blit the unshifted source as before.
 */
int ST16_Preshift_Lookup(const uint8_t *planar,
	int planar_row_bytes,
	const uint8_t *mask,
	int mask_row_bytes,
	int tile_w,
	int tile_h,
	int has_mask,
	int shift,
	ST16_PreshiftView *out);

/* Drop everything. Must run whenever the iconset memory changes (theater load),
 * since entries are keyed by raw address. */
void ST16_Preshift_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_ST16_PRESHIFT_H_ */
