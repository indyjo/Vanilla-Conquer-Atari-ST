/*
 * c2p.h - Chunky-to-planar conversion helpers for Atari ST LoRes (320x200x4bpp)
 *
 * Optimized 1x1 c2p using palette-dependent lookup tables, 4x4 Bayer dithering,
 * and movep for writing 8 pixels (4 planes) at once.
 */

#ifndef ATARILIB_C2P_H
#define ATARILIB_C2P_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	C2P_WEIGHTSET_TEMPERAT = 0,
	C2P_WEIGHTSET_HTITLE = 1
};

/* Select which palette-opt weight table drives 8-bit->4-bit mapping and rebuild dither tables. */
void C2P_Select_WeightSet(int weight_set);
int C2P_Get_WeightSet(void);

/* Map 8-bit palette index to one ST 4-bit color using current dither tables (absolute pixel coords). */
unsigned char C2P_Map8ToPlanar4(int abs_x, int abs_y, unsigned char pal_idx);

/* Convert one 320x200 8-bit buffer to ST planar screen (Physbase). */
void C2P_Render_Logical_To_ST_Screen(const uint8_t *logical, int logical_stride, uint8_t *st_screen);

/*
** Convert an 8bpp row-major rectangle into a small ST-style interleaved planar buffer.
** logical_w should be a multiple of 8 for the fast path; any remainder uses a slow tail.
** Pixels (0,0) of `logical` map to planar (dst_x0,dst_y0). Dither uses abs screen coords
** (abs_x0+lx, abs_y0+ly). planar_row_bytes is the byte stride between scanlines (e.g. 16 for 32-wide).
** planar_width_pixels / planar_height_pixels bound the destination buffer for the slow tail
** (widths not divisible by 8).
*/
void C2P_Render_Logical_To_Planar_Rect(
	const uint8_t *logical,
	int logical_w,
	int logical_h,
	int logical_stride,
	uint8_t *planar_base,
	int planar_row_bytes,
	int planar_width_pixels,
	int planar_height_pixels,
	int dst_x0,
	int dst_y0,
	int abs_x0,
	int abs_y0);

/* ST LoRes planar surface (320x200, 160 bytes/line, interleaved like C2P output). */
#define ST_PLANAR_WIDTH 320
#define ST_PLANAR_HEIGHT 200
#define ST_PLANAR_BYTES_PER_LINE 160
#define ST_PLANAR_SCREEN_BYTES 32000

void ST_Planar_PutPixel(uint8_t *base, int x, int y, unsigned char color4);
unsigned char ST_Planar_GetPixel(const uint8_t *base, int x, int y);
void ST_Planar_Clear(uint8_t *base, unsigned char color4);

/*
** Blit row-major 8bpp source into planar ST buffer at (dst_x,dst_y).
** Clips to [0,320)x[0,200). If trans, skip source pixels == 0.
** Uses C2P_MapDither + PairLUT (same look as full-screen C2P).
*/
void C2P_Blit_Linear8_To_Planar(
	uint8_t *planar_base,
	int dst_x, int dst_y,
	const uint8_t *src, int w, int h, int src_stride,
	int trans);

#ifdef __cplusplus
}
#endif

#endif
