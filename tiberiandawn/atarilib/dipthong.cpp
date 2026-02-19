/*
 * dipthong.cpp - Text extraction functions for Atari ST/MiNT
 */

#include "dipthong.h"
#include <stddef.h>  // For NULL

// String data blocks are stored in little-endian format (matching MIX file format)
// Read bytes individually to handle unaligned pointers and maintain little-endian byte order
static inline unsigned short ReadLE16(const unsigned char *bytes)
{
	return bytes[0] | (bytes[1] << 8);  // Little-endian: low byte first
}

/*
 * Extract_String - Extract a string pointer from a string data block
 * 
 * This is a simplified implementation that assumes the data block
 * contains an array of offsets followed by the string data.
 * String data blocks are stored in little-endian format.
 */
char *Extract_String(void const *data, int string)
{
	if (!data || string < 0) {
		return NULL;
	}
	
	// Read offset byte-by-byte to handle unaligned pointers safely
	// Data is in little-endian format regardless of host endianness
	// The offset array starts at the beginning of the data block
	const unsigned char *bytes = (const unsigned char*)data + (string * sizeof(unsigned short));
	
	// Read little-endian 16-bit offset: low byte first, then high byte
	unsigned short offset = ReadLE16(bytes);
	
	// Return pointer to the string at the calculated offset
	char *result = ((char*)data) + offset;
	
	return result;
}

