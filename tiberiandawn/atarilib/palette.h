/*
 * palette.h - Palette functions for Atari ST/MiNT
 */

#ifndef PALETTE_H
#define PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Palette fading function - matches WIN32LIB signature */
void Fade_Palette_To(void *palette1, unsigned int delay, void (*callback)());

#ifdef __cplusplus
}
#endif

#endif /* PALETTE_H */
