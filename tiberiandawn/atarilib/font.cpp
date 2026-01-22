/*
 * font.cpp - Font global variables for Atari ST/MiNT
 * 
 * This provides font variable definitions compatible with WIN32LIB
 */

#include "font.h"

// Font spacing variables - initialized to 0
extern "C" int FontXSpacing = 0;
extern "C" int FontYSpacing = 0;

// Font dimensions and data
char FontWidth = 0;
char FontHeight = 0;
char *FontWidthBlockPtr = NULL;

// Font pointer
extern "C" void const *FontPtr = NULL;

/***************************************************************************
 * CHAR_PIXEL_WIDTH -- Return pixel width of a character.                 *
 *                                                                         *
 *    Retrieves the pixel width of a character from the font width block. *
 *                                                                         *
 * INPUT:      Character.                                                 *
 *                                                                         *
 * OUTPUT:     Pixel width of a character.                                *
 *                                                                         *
 * WARNINGS:   Set_Font must have been called first.                      *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/font.cpp                                        *
 *=========================================================================*/
int Char_Pixel_Width(char chr)
{
	int width;
	
	if (!FontWidthBlockPtr) {
		return FontWidth + FontXSpacing;  // Fallback to fixed width
	}
	
	width = (unsigned char)*(FontWidthBlockPtr + (unsigned char)chr) + FontXSpacing;
	
	return(width);
}

/***************************************************************************
 * STRING_PIXEL_WIDTH -- Return pixel width of a string of characters.    *
 *                                                                         *
 *    Calculates the pixel width of a string of characters.  This uses     *
 *    the font width block for the widths.                                 *
 *                                                                         *
 * INPUT:      Pointer to string of characters.                           *
 *                                                                         *
 * OUTPUT:     Pixel width of a string of characters.                     *
 *                                                                         *
 * WARNINGS:   Set_Font must have been called first.                      *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/font.cpp                                        *
 *=========================================================================*/
extern "C" unsigned int String_Pixel_Width(char const *string)
{
	unsigned short width;			// Working accumulator of string width.
	unsigned short largest = 0;		// Largest recorded width of the string.
	
	if (!string) return(0);
	
	width = 0;
	while (*string) {
		if (*string == '\r') {
			string++;
			largest = (largest > width) ? largest : width;
			width = 0;
		} else {
			width += Char_Pixel_Width(*string++);	// add each char's width
		}
	}
	largest = (largest > width) ? largest : width;
	return(largest);
}

/***************************************************************************
 * Set_Font -- Changes the default text printing font.                     *
 *                                                                         *
 *    This routine will change the default text printing font for all      *
 *    text output.  It handles updating the system where necessary.        *
 *                                                                         *
 * INPUT:   fontptr  -- Pointer to the font to change to.                  *
 *                                                                         *
 * OUTPUT:  Returns with a pointer to the previous font.                   *
 *                                                                         *
 * WARNINGS:   none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/set_font.cpp                                    *
 *=========================================================================*/

// Helper to read little-endian unsigned short from font file data
static inline unsigned short ReadLE16(const unsigned char* ptr) {
	return ptr[0] | (ptr[1] << 8);
}

void * Set_Font(void const *fontptr)
{
	void *oldfont;
	char const *blockptr;

	oldfont = (void *) FontPtr;

	if (fontptr) {
		FontPtr    = (void *) fontptr;

		/*
		**	Inform the system about the new font.
		**	Font file offsets are stored as little-endian unsigned shorts.
		*/

		const unsigned char* font_bytes = (const unsigned char*)fontptr;
		unsigned short width_block_offset = ReadLE16(font_bytes + FONTWIDTHBLOCK);
		unsigned short info_block_offset = ReadLE16(font_bytes + FONTINFOBLOCK);
		
		FontWidthBlockPtr = (char*)fontptr + width_block_offset;
		blockptr  = (char*)fontptr + info_block_offset;
		FontHeight = *(blockptr + FONTINFOMAXHEIGHT);
		FontWidth  = *(blockptr + FONTINFOMAXWIDTH);
	}

	return(oldfont);
}

/***************************************************************************
 * Set_Font_Palette_Range -- Sets font palette range                      *
 *                                                                         *
 * INPUT:   palette  -- Pointer to palette data (16 bytes for indices 0-15) *
 *          start_idx -- Starting palette index                            *
 *          end_idx   -- Ending palette index                             *
 *                                                                         *
 * OUTPUT:  none                                                           *
 *                                                                         *
 * WARNINGS:   none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB - updates ColorXlat table                      *
 *=========================================================================*/
extern "C" void Set_Font_Palette_Range(void const *palette, int start_idx, int end_idx)
{
	if (!palette) return;
	
	// Import ColorXlat from drawbuff.cpp
	extern unsigned char ColorXlat[256];
	
	const unsigned char *pal = (const unsigned char *)palette;
	
	// Update ColorXlat table for the specified range
	// The palette array contains color values for indices 0-15
	// Each palette index maps directly to ColorXlat[index]
	for (int i = start_idx; i <= end_idx && i < 16; i++) {
		ColorXlat[i] = pal[i];
	}
}

