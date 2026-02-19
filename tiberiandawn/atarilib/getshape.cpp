/*
 * getshape.cpp - Shape extraction functions for Atari ST/MiNT
 * 
 * Handles endianness conversion for shape files (stored in little-endian format).
 */

#include "shape.h"
#include <stddef.h>  // For NULL

// Helper to read little-endian 32-bit values
static inline unsigned long ReadLE32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | 
	       ((unsigned long)bytes[1] << 8) | 
	       ((unsigned long)bytes[2] << 16) | 
	       ((unsigned long)bytes[3] << 24);
}

// Helper to read little-endian 16-bit values
static inline unsigned short ReadLE16(const unsigned char *bytes)
{
	return (unsigned short)bytes[0] | ((unsigned short)bytes[1] << 8);
}

/***************************************************************************
 * Extract_Shape_Count -- returns # of shapes in the given shape block   *
 *                                                                         *
 * INPUT:                                                                  *
 * buffer	pointer to shape block                                          *
 *                                                                         *
 * OUTPUT:                                                                 *
 * # shapes in the block                                                   *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Extract_Shape_Count(void const *buffer)
{
	if (!buffer) return 0;
	
	// NumShapes is the first 16-bit value, stored as little-endian
	const unsigned char *bytes = (const unsigned char*)buffer;
	return (int)ReadLE16(bytes);
}

/***************************************************************************
 * Extract_Shape -- Gets pointer to shape in given shape block            *
 *                                                                         *
 * INPUT:                                                                  *
 * buffer	pointer to shape block                                          *
 * shape	index of shape to get                                            *
 *                                                                         *
 * OUTPUT:                                                                 *
 * pointer to shape in the shape block                                     *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
void *Extract_Shape(void const *buffer, int shape)
{
	if (!buffer || shape < 0) {
		return NULL;
	}
	
	const unsigned char *bytes = (const unsigned char*)buffer;
	
	// Read NumShapes (first 16-bit value, little-endian)
	unsigned short num_shapes = ReadLE16(bytes);
	
	if (shape >= num_shapes) {
		return NULL;
	}
	
	// Offsets array starts at offset 2 (after NumShapes)
	// Each offset is a 32-bit little-endian value
	// Read the offset for the requested shape
	unsigned long offset = ReadLE32(bytes + 2 + (shape * 4));
	
	// Return pointer to shape data (offset is from start of block, but skip NumShapes)
	return (void*)(bytes + 2 + offset);
}

/***************************************************************************
 * Get_Shape_Width -- gets shape width in pixels                           *
 *                                                                         *
 * INPUT:                                                                  *
 * shape	pointer to a shape                                               *
 *                                                                         *
 * OUTPUT:                                                                 *
 * shape width in pixels                                                   *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Get_Shape_Width(void const *shape)
{
	if (!shape) return 0;
	
	const unsigned char *data = (const unsigned char*)shape;
	
	// Shape_Type structure (little-endian):
	// unsigned short ShapeType (offset 0-1)
	// unsigned char Height (offset 2)
	// unsigned short Width (offset 3-4 or 4-5 depending on alignment)
	// Try both packed and aligned layouts
	unsigned short width_packed = ReadLE16(data + 3);
	unsigned short width_aligned = ReadLE16(data + 4);
	
	// Width is in bytes, not pixels (for 8-bit pixels, bytes = pixels)
	// Use the value that's reasonable (typically 1-64 bytes for cursors)
	if (width_packed > 0 && width_packed <= 64) {
		return (int)width_packed;
	} else if (width_aligned > 0 && width_aligned <= 64) {
		return (int)width_aligned;
	}
	
	// Default to aligned if neither looks right
	return (int)width_aligned;
}

/***************************************************************************
 * Get_Shape_Height -- gets shape height in pixels                         *
 *                                                                         *
 * INPUT:                                                                  *
 * shape	pointer to a shape                                               *
 *                                                                         *
 * OUTPUT:                                                                 *
 * shape height in pixels                                                  *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Get_Shape_Height(void const *shape)
{
	if (!shape) return 0;
	
	const unsigned char *data = (const unsigned char*)shape;
	
	// Height is at offset 2 (unsigned char)
	return (int)data[2];
}

/***************************************************************************
 * Get_Shape_Uncomp_Size -- gets shape's uncompressed size in bytes       *
 *                                                                         *
 * INPUT:                                                                  *
 * shape	pointer to shape                                                 *
 *                                                                         *
 * OUTPUT:                                                                 *
 * shape's size in bytes when uncompressed                                 *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Get_Shape_Uncomp_Size(void const *shape)
{
	if (!shape) return 0;
	
	const unsigned char *data = (const unsigned char*)shape;
	
	// DataLength is at offset 8-9 or 10-11 depending on alignment (little-endian)
	unsigned short len_packed = ReadLE16(data + 8);
	unsigned short len_aligned = ReadLE16(data + 10);
	
	// Use the value that's reasonable
	if (len_packed > 0 && len_packed < 65535) {
		return (int)len_packed;
	} else if (len_aligned > 0 && len_aligned < 65535) {
		return (int)len_aligned;
	}
	
	// Default to aligned
	return (int)len_aligned;
}
