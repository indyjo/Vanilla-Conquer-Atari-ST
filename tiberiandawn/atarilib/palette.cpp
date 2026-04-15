/*
 * palette.cpp - Palette functions for Atari ST/MiNT
 */

#include "palette.h"
#include "c2p.h"
#include "st_temperat_palette.h"

extern unsigned char *GamePalette;

/* Current palette buffer - copy of current DAC register values */
/* Initialized to 255 (white) to match WIN32LIB behavior */
extern "C" unsigned char CurrentPalette[768] = {255};

/* Palette mapping: maps each original palette entry (0-255) to Atari ST color index (0-15) */
/* Initialized to map all entries to color 0 (black) */
extern "C" unsigned char PaletteToST[256] = {0};

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
// Helper function to calculate brightness from RGB
static int CalculateBrightness(unsigned char r, unsigned char g, unsigned char b)
{
	// Standard brightness formula: 0.299*R + 0.587*G + 0.114*B
	// Using integer arithmetic for speed: (77*R + 150*G + 29*B) / 256
	return (77 * r + 150 * g + 29 * b) >> 8;
}

static void Install_ST_Hardware_Palette_First16(const unsigned char *pal768)
{
	if (!pal768) return;
#if defined(__m68k__)
	St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal768);
#endif
}

extern "C" void Set_Palette(void *palette)
{
	if (!palette) return;
	
	// Copy palette to CurrentPalette
	unsigned char *pal = (unsigned char *)palette;
	if (pal == GamePalette) {
		C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	}
	for (int i = 0; i < 768; i++) {
		CurrentPalette[i] = pal[i] & 63;
	}

	/* Keep ST hardware pens in sync with whichever logical palette got installed. */
	Install_ST_Hardware_Palette_First16(CurrentPalette);
	
	// Build brightness-based mapping from original palette (256 entries) to ST colors (16 entries)
	// Map source brightness (0-63) directly to ST palette index (0-15) by bitshifting
	// CalculateBrightness returns 0-255, scale to 0-63 then >> 2 maps to ST index 0-15
	for (int pal_idx = 0; pal_idx < 256; pal_idx++) {
		unsigned char r = CurrentPalette[pal_idx * 3 + 0];
		unsigned char g = CurrentPalette[pal_idx * 3 + 1];
		unsigned char b = CurrentPalette[pal_idx * 3 + 2];
		
		// Calculate brightness (0-255), scale to 0-63 then map to ST palette index (0-15)
		int brightness = CalculateBrightness(r, g, b);
		PaletteToST[pal_idx] = (unsigned char)(brightness >> 2);
	}

	/* Rebuild c2p palette+dither tables (STDOOM-style). */
	C2P_Rebuild_Tables_From_CurrentPalette();
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

