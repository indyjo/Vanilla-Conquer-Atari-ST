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

/* Rebuild palette-dependent mapping table after Set_Palette(). */
void C2P_Rebuild_Tables_From_CurrentPalette(void);

/* Convert one 320x200 8-bit buffer to ST planar screen (Physbase). */
void C2P_Render_Logical_To_ST_Screen(const uint8_t *logical, int logical_stride, uint8_t *st_screen);

#ifdef __cplusplus
}
#endif

#endif
