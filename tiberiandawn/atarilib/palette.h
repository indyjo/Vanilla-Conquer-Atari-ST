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

/* Palette mapping: maps each original palette entry (0-255) to Atari ST color index (0-15) */
extern unsigned char PaletteToST[256];

#ifdef __cplusplus
}
#endif

#endif /* PALETTE_H */
