/*
 * palette.cpp - Palette functions for Atari ST/MiNT
 */

#include "palette.h"

/* Current palette buffer - copy of current DAC register values */
/* Initialized to 255 (white) to match WIN32LIB behavior */
extern "C" unsigned char CurrentPalette[768] = {255};

/* Stub implementation of palette fading - matches WIN32LIB signature */
void Fade_Palette_To(void *palette1, unsigned int delay, void (*callback)())
{
	// Stub - palette fading not implemented
	// In a real implementation, this would fade to the palette over time
	if (callback) {
		callback();
	}
}

