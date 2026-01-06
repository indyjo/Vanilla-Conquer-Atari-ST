/*
 * misc.cpp - Miscellaneous functions for Atari ST/MiNT
 * 
 * This provides implementations compatible with WIN32LIB
 */

#include "misc.h"

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
	
	// Calculate number of 4-byte chunks and remainder bytes
	unsigned long num_chunks = local_length >> 2;  // Divide by 4
	unsigned long remainder = local_length & 3;    // Modulo 4 (0-3 bytes)
	
	// Process 4-byte chunks
	unsigned long *dword_ptr = (unsigned long *)data;
	for (unsigned long i = 0; i < num_chunks; i++) {
		unsigned long value = dword_ptr[i];
		
		// Rotate CRC left by 1 bit
		unsigned long high_bit = (crc & 0x80000000UL) ? 1UL : 0UL;
		crc = (crc << 1) | high_bit;
		
		// Add the 4-byte value
		crc += value;
	}
	
	// Handle remainder bytes (1-3 bytes)
	if (remainder > 0) {
		unsigned long remainder_value = 0;
		unsigned char *remainder_ptr = data + (num_chunks * 4);
		
		// Load remainder bytes, rotating right by 8 bits each time
		for (unsigned long i = 0; i < remainder; i++) {
			remainder_value = (remainder_value >> 8) | ((unsigned long)remainder_ptr[i] << 24);
		}
		
		// Rotate the remainder value to align it properly
		// This matches the assembly: neg ecx, add ecx, 4, shl ecx, 3, ror eax, cl
		unsigned long shift = (4 - remainder) * 8;
		remainder_value = (remainder_value >> shift) | (remainder_value << (32 - shift));
		
		// Rotate CRC left by 1 bit and add remainder
		unsigned long high_bit = (crc & 0x80000000UL) ? 1UL : 0UL;
		crc = (crc << 1) | high_bit;
		crc += remainder_value;
	}
	
	return (long)crc;
}

/*=========================================================================*/
/* Build_Fading_Table -- Builds a fading table for palette remapping      */
/*                                                                         */
/* This is a stub implementation matching WIN32LIB signature            */
/*                                                                         */
/* INPUT:                                                                  */
/*   palette  -- Source palette (256 colors * 3 bytes = 768 bytes)        */
/*   dest     -- Destination buffer for fading table (256 bytes)          */
/*   color    -- Target color index                                       */
/*   frac     -- Fading fraction (0-255)                                  */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns pointer to dest                                              */
/*=========================================================================*/

extern "C" void *Build_Fading_Table(void const *palette, void const *dest, long int color, long int frac)
{
	if (!palette || !dest) {
		return (void *)dest;
	}

	// Stub implementation - just copy palette indices for now
	// A full implementation would calculate faded colors and find closest matches
	unsigned char *dest_ptr = (unsigned char *)dest;
	for (int i = 0; i < 256; i++) {
		dest_ptr[i] = (unsigned char)i;
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
 * Set_Bit -- Sets a bit in a bit array                                    *
 *                                                                         *
 * INPUT:		void *array - pointer to bit array                          *
 *					int bit - bit index to set                                *
 *					int value - value to set (0 or non-zero)                 *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to portable C)       *
 *=========================================================================*/
extern "C" void Set_Bit(void * array, int bit, int value)
{
	if (!array) return;
	
	unsigned long *dword_array = (unsigned long *)array;
	int dword_index = bit >> 5;  // Divide by 32
	int bit_index = bit & 0x1F;   // Modulo 32 (0-31)
	
	// Clear the bit first
	unsigned long mask = ~(1UL << bit_index);
	dword_array[dword_index] &= mask;
	
	// Set the bit if value is non-zero
	if (value) {
		mask = (1UL << bit_index);
		dword_array[dword_index] |= mask;
	}
}

/***************************************************************************
 * Get_Bit -- Gets a bit from a bit array                                 *
 *                                                                         *
 * INPUT:		void const *array - pointer to bit array                    *
 *					int bit - bit index to get                                *
 *                                                                         *
 * OUTPUT:     int - bit value (0 or 1)                                    *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to portable C)       *
 *=========================================================================*/
extern "C" int Get_Bit(void const * array, int bit)
{
	if (!array) return 0;
	
	unsigned long *dword_array = (unsigned long *)array;
	int dword_index = bit >> 5;  // Divide by 32
	int bit_index = bit & 0x1F;   // Modulo 32 (0-31)
	
	return (dword_array[dword_index] >> bit_index) & 1;
}

/***************************************************************************
 * First_True_Bit -- Finds the first set bit in a bit array               *
 *                                                                         *
 * INPUT:		void const *array - pointer to bit array                    *
 *                                                                         *
 * OUTPUT:     int - bit index of first set bit, or -1 if none found       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to portable C)       *
 *=========================================================================*/
extern "C" int First_True_Bit(void const * array)
{
	if (!array) return -1;
	
	unsigned long *dword_array = (unsigned long *)array;
	int bit_offset = -32;
	
	// Search through dwords until we find a set bit
	for (int i = 0; ; i++) {
		bit_offset += 32;
		unsigned long dword = dword_array[i];
		
		if (dword != 0) {
			// Find the first set bit in this dword
			for (int j = 0; j < 32; j++) {
				if (dword & (1UL << j)) {
					return bit_offset + j;
				}
			}
		}
	}
	
	return -1;  // Should never reach here
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
 *=========================================================================*/
extern "C" int First_False_Bit(void const * array)
{
	if (!array) return -1;
	
	unsigned long *dword_array = (unsigned long *)array;
	int bit_offset = -32;
	
	// Search through dwords until we find a clear bit
	for (int i = 0; ; i++) {
		bit_offset += 32;
		unsigned long dword = ~dword_array[i];  // Invert to find clear bits
		
		if (dword != 0) {
			// Find the first set bit in the inverted dword
			for (int j = 0; j < 32; j++) {
				if (dword & (1UL << j)) {
					return bit_offset + j;
				}
			}
		}
	}
	
	return -1;  // Should never reach here
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
	// Stub for Atari ST - vertical blank synchronization not needed
	// In a real implementation, this would wait for the VBL interrupt
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

