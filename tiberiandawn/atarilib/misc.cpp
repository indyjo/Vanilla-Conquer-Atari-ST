/*
 * misc.cpp - Miscellaneous functions for Atari ST/MiNT
 * 
 * This provides implementations compatible with WIN32LIB
 */

#include "misc.h"
#include <mint/osbind.h>

/*=========================================================================*/
/* Calculate_CRC -- Computes a CRC value for a data buffer                 */
/*                                                                         */
/* This is a portable C implementation of the original x86 assembly        */
/* function from CRC.ASM (Joe L. Bostic, June 12, 1992)                   */
/*                                                                         */
/* INPUT:                                                                  */
/*   buffer  -- Pointer to data buffer                                     */
/*   length  -- Length of buffer in bytes                                  */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns CRC value as a long                                           */
/*=========================================================================*/

extern "C" long Calculate_CRC(void *buffer, long length)
{
	if (!buffer || length <= 0) {
		return 0;
	}

	unsigned long crc = 0;
	unsigned char *data = (unsigned char *)buffer;
	unsigned long local_length = (unsigned long)length;
	
	// Calculate number of 4-byte chunks (round up by padding with zeros)
	unsigned long num_chunks = (local_length + 3) >> 2;  // Divide by 4, rounding up
	
	// Process all chunks uniformly (remainder bytes are implicitly zero-padded)
	// IMPORTANT: Read bytes individually and construct dword in little-endian order
	// to match the x86 assembly behavior (lodsd loads in little-endian)
	for (unsigned long i = 0; i < num_chunks; i++) {
		unsigned char *chunk_ptr = data + (i * 4);
		unsigned long bytes_available = local_length - (i * 4);
		
		// Construct dword from bytes in little-endian order
		// Missing bytes (if any) are implicitly zero, matching the padding behavior
		unsigned long value = ((bytes_available > 0) ? (unsigned long)chunk_ptr[0] : 0UL) |
		                     ((bytes_available > 1) ? ((unsigned long)chunk_ptr[1] << 8) : 0UL) |
		                     ((bytes_available > 2) ? ((unsigned long)chunk_ptr[2] << 16) : 0UL) |
		                     ((bytes_available > 3) ? ((unsigned long)chunk_ptr[3] << 24) : 0UL);
		
		// Rotate CRC left by 1 bit
		unsigned long high_bit = (crc & 0x80000000UL) ? 1UL : 0UL;
		crc = (crc << 1) | high_bit;
		
		// Add the 4-byte value
		crc += value;
	}
	
	return (long)crc;
}

/*=========================================================================*/
/* Build_Fading_Table -- Builds a fading table for palette remapping      */
/*                                                                         */
/* C port of WIN32LIB/DrawMisc.cpp (from PAL.ASM): each entry i maps to   */
/* the palette index whose RGB is closest to lerping color i toward        */
/* palette[color] by frac/255. Index 0 is never remapped (transparent).   */
/* Required for SHAPE_GHOST / UnitShadow on ST (misc stub used to be id). */
/*=========================================================================*/

extern "C" void *Build_Fading_Table(void const *palette, void const *dest, long int color, long int frac)
{
	if (!palette || !dest) {
		return (void *)dest;
	}

	const unsigned char *const pal = (const unsigned char *)palette;
	unsigned char *const out = (unsigned char *)dest;

	if (color < 0 || color > 255) {
		return (void *)dest;
	}
	if (frac < 0) {
		frac = 0;
	}
	if (frac > 255) {
		frac = 255;
	}

	const int tr = (int)pal[color * 3 + 0];
	const int tg = (int)pal[color * 3 + 1];
	const int tb = (int)pal[color * 3 + 2];

	out[0] = 0;

	for (int i = 1; i < 256; ++i) {
		const int or_ = (int)pal[i * 3 + 0];
		const int og = (int)pal[i * 3 + 1];
		const int ob = (int)pal[i * 3 + 2];
		const int ir = or_ - (or_ - tr) * (int)frac / 255;
		const int ig = og - (og - tg) * (int)frac / 255;
		const int ib = ob - (ob - tb) * (int)frac / 255;

		int best_j = color;
		unsigned long best_d = 0xFFFFFFFFUL;

		for (int j = 1; j < 256; ++j) {
			if (j == i) {
				continue;
			}
			const int pr = (int)pal[j * 3 + 0];
			const int pg = (int)pal[j * 3 + 1];
			const int pb = (int)pal[j * 3 + 2];
			const long dr = (long)pr - (long)ir;
			const long dg = (long)pg - (long)ig;
			const long db = (long)pb - (long)ib;
			const unsigned long d2 =
					(unsigned long)(dr * dr + dg * dg + db * db);
			if (d2 < best_d) {
				best_d = d2;
				best_j = j;
				if (d2 == 0) {
					break;
				}
			}
		}
		out[i] = (unsigned char)best_j;
	}

	return (void *)dest;
}

// Icon cache stubs - not needed for ATARILIB
void Restore_Cached_Icons(void)
{
	// Stub - no icon caching on Atari ST
}

void Invalidate_Cached_Icons(void)
{
	// Stub - no icon caching on Atari ST
}

/***************************************************************************
 * SurfaceMonitorClass::SurfaceMonitorClass -- Constructor                 *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
SurfaceMonitorClass::SurfaceMonitorClass(void)
{
	for (int i=0 ; i<MAX_SURFACES ; i++)
	{
		Surface[i]=NULL;
	}
	InFocus=FALSE;
	SurfacesRestored=FALSE;
}

/***************************************************************************
 * SurfaceMonitorClass::Add_DD_Surface -- Add a surface to the list       *
 *                                                                         *
 * INPUT:		LPDIRECTDRAWSURFACE surface - surface to add               *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
void SurfaceMonitorClass::Add_DD_Surface(LPDIRECTDRAWSURFACE surface)
{
	// Stub for Atari ST - no DirectDraw surfaces
	(void)surface;
}

/***************************************************************************
 * SurfaceMonitorClass::Remove_DD_Surface -- Remove a surface from list   *
 *                                                                         *
 * INPUT:		LPDIRECTDRAWSURFACE surface - surface to remove             *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
void SurfaceMonitorClass::Remove_DD_Surface(LPDIRECTDRAWSURFACE surface)
{
	// Stub for Atari ST - no DirectDraw surfaces
	(void)surface;
}

/***************************************************************************
 * SurfaceMonitorClass::Got_Surface_Already -- Check if surface in list   *
 *                                                                         *
 * INPUT:		LPDIRECTDRAWSURFACE surface - surface to check              *
 *                                                                         *
 * OUTPUT:     BOOL - TRUE if surface is in list                          *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
BOOL SurfaceMonitorClass::Got_Surface_Already(LPDIRECTDRAWSURFACE surface)
{
	// Stub for Atari ST - no DirectDraw surfaces
	(void)surface;
	return FALSE;
}

/***************************************************************************
 * SurfaceMonitorClass::Restore_Surfaces -- Restore all surfaces          *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
void SurfaceMonitorClass::Restore_Surfaces(void)
{
	// Stub for Atari ST - no DirectDraw surfaces
	SurfacesRestored = TRUE;
}

/***************************************************************************
 * SurfaceMonitorClass::Set_Surface_Focus -- Set focus state              *
 *                                                                         *
 * INPUT:		BOOL in_focus - focus state                                 *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
void SurfaceMonitorClass::Set_Surface_Focus(BOOL in_focus)
{
	InFocus = in_focus;
}

/***************************************************************************
 * SurfaceMonitorClass::Release -- Release all surfaces                    *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/ddraw.cpp (simplified for Atari ST)            *
 *=========================================================================*/
void SurfaceMonitorClass::Release(void)
{
	// Stub for Atari ST - no DirectDraw surfaces
	for (int i=0 ; i<MAX_SURFACES ; i++)
	{
		Surface[i]=NULL;
	}
	InFocus=FALSE;
	SurfacesRestored=FALSE;
}

// Global instance of SurfaceMonitorClass
SurfaceMonitorClass AllSurfaces;

/***************************************************************************
 * First_True_Bit -- Finds the first set bit in a bit array               *
 *                                                                         *
 * INPUT:		void const *array - pointer to bit array                    *
 *                                                                         *
 * OUTPUT:     int - bit index of first set bit, or -1 if none found       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to portable C)       *
 *   Modified to work with byte arrays (for BooleanVectorClass)           *
 *   Note: This function searches up to 256 bytes (2048 bits)              *
 *=========================================================================*/
extern "C" int First_True_Bit(void const * array)
{
	if (!array) return -1;
	
	unsigned char const *byte_array = (unsigned char const *)array;
	
	// Search through bytes (up to 256 bytes = 2048 bits to prevent infinite loop)
	for (int byte_idx = 0; byte_idx < 256; byte_idx++) {
		unsigned char byte = byte_array[byte_idx];
		
		if (byte != 0) {
			// Find the first set bit in this byte
			for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
				if (byte & (1U << bit_idx)) {
					return (byte_idx * 8) + bit_idx;
				}
			}
		}
	}
	
	return -1;  // No set bit found
}

/***************************************************************************
 * First_False_Bit -- Finds the first clear bit in a bit array            *
 *                                                                         *
 * INPUT:		void const *array - pointer to bit array                    *
 *                                                                         *
 * OUTPUT:     int - bit index of first clear bit, or -1 if none found     *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to portable C)       *
 *   Modified to work with byte arrays (for BooleanVectorClass)           *
 *   Note: This function searches up to 256 bytes (2048 bits)              *
 *=========================================================================*/
extern "C" int First_False_Bit(void const * array)
{
	if (!array) return -1;
	
	unsigned char const *byte_array = (unsigned char const *)array;
	
	// Search through bytes (up to 256 bytes = 2048 bits to prevent infinite loop)
	for (int byte_idx = 0; byte_idx < 256; byte_idx++) {
		unsigned char byte = byte_array[byte_idx];
		
		// Check if this byte has any clear bits (not all bits are set)
		if (byte != 0xFF) {
			// Find the first clear bit in this byte
			for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
				if (!(byte & (1U << bit_idx))) {
					return (byte_idx * 8) + bit_idx;
				}
			}
		}
	}
	
	return -1;  // No clear bit found
}

/***************************************************************************
 * Bound -- Bounds a value between min and max                             *
 *                                                                         *
 * INPUT:		int original - value to bound                               *
 *					int min - minimum value                                   *
 *					int max - maximum value                                   *
 *                                                                         *
 * OUTPUT:     int - bounded value                                         *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to portable C)       *
 *=========================================================================*/
extern "C" int Bound(int original, int min, int max)
{
	// Ensure min <= max
	if (min > max) {
		int temp = min;
		min = max;
		max = temp;
	}
	
	// Bound the value
	if (original < min) {
		return min;
	}
	if (original > max) {
		return max;
	}
	
	return original;
}

/***************************************************************************
 * Confine_Rect -- Confines a rectangle to fit within a clipping window   *
 *                                                                         *
 * INPUT:   x, y - pointers to rectangle position (modified)              *
 *          dw, dh - width and height of rectangle                        *
 *          width, height - dimensions of clipping window                 *
 *                                                                         *
 * OUTPUT:  0 if rectangle is contained, positive if it was shifted       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/DrawMisc.cpp (x86 assembly to C)               *
 *=========================================================================*/
int Confine_Rect(int *x, int *y, int dw, int dh, int width, int height)
{
	if (!x || !y) return 0;
	
	int result = 0;
	int x0 = *x;
	int y0 = *y;
	int x1 = x0 + dw;
	int y1 = y0 + dh;
	
	// Confine X axis
	if (x0 < 0) {
		*x = 0;
		result = 1;
	} else if (x1 > width) {
		*x = width - dw;
		if (*x < 0) *x = 0;
		result = 1;
	}
	
	// Confine Y axis
	if (y0 < 0) {
		*y = 0;
		result = 1;
	} else if (y1 > height) {
		*y = height - dh;
		if (*y < 0) *y = 0;
		result = 1;
	}
	
	return result;
}

// Global variable: Can video driver blit overlapped regions?
BOOL OverlappedVideoBlits = FALSE;

// Global variables for icon caching statistics
int CachedIconsDrawn = 0;
int UnCachedIconsDrawn = 0;

/***************************************************************************
 * Wait_Vert_Blank -- Waits for vertical blank                              *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST - vertical blank not needed                         *
 *=========================================================================*/
void Wait_Vert_Blank(void)
{
	Vsync();
}

/***************************************************************************
 * Clip_Rect -- Clip a rectangle against a clipping window                 *
 *                                                                         *
 * INPUT:   x, y - pointers to rectangle position (modified)               *
 *          dw, dh - pointers to rectangle size (modified)                 *
 *          width, height - dimensions of clipping window                  *
 *                                                                         *
 * OUTPUT:  0 if contained, negative if outside, positive if clipped      *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/DrawMisc.cpp (simplified C implementation)      *
 *=========================================================================*/
int Clip_Rect(int *x, int *y, int *dw, int *dh, int width, int height)
{
	if (!x || !y || !dw || !dh) return -1;
	
	int x0 = *x;
	int y0 = *y;
	int x1 = x0 + *dw;
	int y1 = y0 + *dh;
	
	// Check if completely outside
	if (x1 < 0 || x0 >= width || y1 < 0 || y0 >= height) {
		return -1;  // Completely outside
	}
	
	// Clip to window
	if (x0 < 0) {
		*dw += x0;
		*x = 0;
	}
	if (y0 < 0) {
		*dh += y0;
		*y = 0;
	}
	if (x1 > width) {
		*dw = width - *x;
	}
	if (y1 > height) {
		*dh = height - *y;
	}
	
	// Check if still valid after clipping
	if (*dw <= 0 || *dh <= 0) {
		return -1;  // Invalid after clipping
	}
	
	// Return 0 if no clipping was needed, positive if clipping occurred
	return (x0 != *x || y0 != *y || x1 != (*x + *dw) || y1 != (*y + *dh)) ? 1 : 0;
}

