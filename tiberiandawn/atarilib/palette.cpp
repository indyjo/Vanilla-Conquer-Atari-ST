/*
 * palette.cpp - Palette functions for Atari ST/MiNT
 */

#include "palette.h"

/* Current palette buffer - copy of current DAC register values */
/* Initialized to 255 (white) to match WIN32LIB behavior */
extern "C" unsigned char CurrentPalette[768] = {255};

/***************************************************************************
 * Set_Palette -- sets the current palette                                *
 *                                                                         *
 * INPUT:		void *palette - palette to set                              *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/palette.cpp (simplified for Atari ST)          *
 *=========================================================================*/
extern "C" void Set_Palette(void *palette)
{
	if (!palette) return;
	
	// Copy palette to CurrentPalette
	unsigned char *pal = (unsigned char *)palette;
	for (int i = 0; i < 768; i++) {
		CurrentPalette[i] = pal[i];
	}
	
	// TODO: Implement actual palette setting for Atari ST
	// This would typically involve setting the hardware palette registers
}

/* Stub implementation of palette fading - matches WIN32LIB signature */
void Fade_Palette_To(void *palette1, unsigned int delay, void (*callback)())
{
	// Stub - palette fading not implemented
	// In a real implementation, this would fade to the palette over time
	if (callback) {
		callback();
	}
}

