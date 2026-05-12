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
/* Register a custom 256×16 weight table: LUTs are rebuilt from it during this call only (buffer may be freed after return). */
int C2P_Install_CustomWeights(const uint8_t *weights_256x16);
/* Rebuild LUTs from the built-in table for the current weight set (TEMPERAT / HTITLE). */
void C2P_Clear_CustomWeights(void);

/* Map 8-bit palette index to one ST 4-bit color using current dither tables (absolute pixel coords). */
unsigned char C2P_Map8ToPlanar4(int abs_x, int abs_y, unsigned char pal_idx);
/* Nearest-map LUT (8-bit VGA index -> ST 4-bit index). */
extern uint8_t C2P_MapNearestLUT[256];
/* Inline nearest-map helper (no spatial dithering). */
static inline unsigned char C2P_Map8ToNearest4(unsigned char pal_idx)
{
	return C2P_MapNearestLUT[pal_idx];
}
/*
 * True if active 256->16 weight row has exactly one non-zero entry; optionally returns that color.
 * Bit 7 in C2P_PaletteIndexClean4LUT[] set => clean; low nibble is ST color (0x80 means clean @ 0).
 */
extern uint8_t C2P_PaletteIndexClean4LUT[256];
static inline int C2P_Is_Palette_Index_Clean4(uint8_t pal_idx, uint8_t *out_color4)
{
	const uint8_t e = C2P_PaletteIndexClean4LUT[pal_idx];
	if ((e & 0x80u) == 0)
		return 0;
	if (out_color4)
		*out_color4 = e & 0x0Fu;
	return 1;
}

/*
 * Convert 8-bit chunky rows to ST 320×200 interleaved planar (e.g. Physbase).
 * Uses the STDOOM atari_c2p lorez inner loop (movem + fragment LUT + movep) on m68k.
 * Renders scanlines y = start_line_y, start_line_y + line_y_step, ... while y < 200.
 * Full frame: start_line_y = 0, line_y_step = 1.
 * Interlaced fields (half the lines per call): line_y_step = 2 with start_line_y 0 or 1 on
 * alternating calls (even / odd raster lines).
 */
void C2P_Render_Logical_To_ST_Screen(
	const uint8_t *logical,
	int logical_stride,
	uint8_t *st_screen,
	int start_line_y,
	int line_y_step);

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

/* ST LoRes planar: canonical 320×200 screen; interleaved layout needs width multiple of 16. */
#define ST_PLANAR_WIDTH 320
#define ST_PLANAR_HEIGHT 200
#define ST_PLANAR_BYTES_PER_LINE 160
#define ST_PLANAR_SCREEN_BYTES 32000

/* Bytes per scanline for 16-color interleaved ST planar (4 planes, 8 pixels per movep group). */
static inline int ST_Planar_Row_Bytes(int width_pixels)
{
	return width_pixels / 2;
}

void ST_Planar_PutPixel(uint8_t *base, int row_bytes, int pw, int ph, int x, int y, unsigned char color4);
unsigned char ST_Planar_GetPixel(const uint8_t *base, int row_bytes, int pw, int ph, int x, int y);
void ST_Planar_Clear(uint8_t *base, int row_bytes, int pw, int ph, unsigned char color4);

/*
** Fill an 8-pixel-aligned rectangle in an ST-style interleaved planar surface.
** Coordinates are relative to planar_base; dst_x and pixel_width must both be multiples of 8.
** Uses the same 8-bit palette index -> dithered 4-bit ST mapping as PutPixel/C2P.
*/
void C2P_Fill_Aligned8_Rect(
	uint8_t *planar_base,
	int planar_row_bytes,
	int planar_width_pixels,
	int planar_height_pixels,
	int dst_x,
	int dst_y,
	int pixel_width,
	int pixel_height,
	unsigned char pal_idx);

/*
** Blit row-major 8bpp source into planar ST buffer at (dst_x,dst_y).
** Clips to [0,320)x[0,200). If trans, skip source pixels == 0.
** Uses C2P_MapDither + PairLUT (same look as full-screen C2P).
*/
void C2P_Blit_Linear8_To_Planar(
	uint8_t *planar_base,
	int dst_x, int dst_y,
	const uint8_t *src, int w, int h, int src_stride,
	int trans,
	int planar_row_bytes,
	int planar_width_pixels,
	int planar_height_pixels);

#ifdef __cplusplus
}
#endif

#endif
