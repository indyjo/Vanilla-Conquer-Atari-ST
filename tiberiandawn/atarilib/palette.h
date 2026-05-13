/*
 * palette.h - Palette functions for Atari ST/MiNT
 */

#ifndef PALETTE_H
#define PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Set the current palette */
void Set_Palette(void *palette);

/* Palette fading function - matches WIN32LIB signature */
void Fade_Palette_To(void *palette1, unsigned int delay, void (*callback)());

/* Current palette buffer - copy of current DAC register values */
extern unsigned char CurrentPalette[768];

/*
 * STE hardware palette ($FF8240..): one-time snapshot of TOS pens before the game
 * overwrites them with Set_Palette; restore at exit before/around returning the shifter to TOS.
 */
void Palette_ST_Capture_Hardware_State_Once(void);
void Palette_ST_Restore_Hardware_State_And_Clear(void);

/*
 * Fill a row-major 8bpp buffer with a 16×16 grid of palette indices 0..255
 * (8x8 px per cell, centered horizontally and vertically). Same layout as tests/st_suite/st_interactive.cpp.
 * `row_stride_bytes` >= `width_pixels`; only the first `width_pixels` bytes of each row are cleared/filled.
 */
void Palette_Debug_Fill_Index_Grid_Chunky(
	unsigned char *chunky,
	int width_pixels,
	int height_pixels,
	int row_stride_bytes);

/*
 * Draw a 16x16 grid of palette indices 0..255 (8x8 px per cell, centered on screen)
 * directly into ST interleaved planar 320x200 using C2P_Fill_Aligned8_Rect — no chunky scratch.
 * Clears the full planar buffer first (memset to 0). If `rgb768_preview` is non-NULL,
 * installs it with Set_Palette before drawing so swatches match that LUT.
 */
void Palette_Debug_Draw_Index_Grid_To_Planar320(
	unsigned char *planar_base,
	int planar_row_bytes,
	const unsigned char *rgb768_preview);

#ifdef __cplusplus
}
#endif

#endif /* PALETTE_H */
