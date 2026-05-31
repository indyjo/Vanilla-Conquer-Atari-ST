/*
 * palette.h - Palette functions for Atari ST/MiNT
 */

#ifndef PALETTE_H
#define PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

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
 * Fill a row-major 8bpp buffer with a row of 16 ST hardware pens (8×8 each,
 * chunky indices from C2P_HW_Palette_Subset), then 2 px gap, then a 16×16 grid
 * of palette indices 0..255 (8×8 px per cell). Block is centered on the buffer.
 * `row_stride_bytes` >= `width_pixels`; only the first `width_pixels` bytes of each row are cleared/filled.
 */
void Palette_Debug_Fill_Index_Grid_Chunky(
	unsigned char *chunky,
	int width_pixels,
	int height_pixels,
	int row_stride_bytes);

/*
 * Draw the same 16-pen ST strip + 2 px gap + 16×16 index grid as Palette_Debug_Fill_Index_Grid_Chunky,
 * directly into ST interleaved planar 320×200 using C2P_Fill_Aligned8_Rect — no chunky scratch.
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
