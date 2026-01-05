/*
 * dipthong.cpp - Text extraction functions for Atari ST/MiNT
 */

#include "dipthong.h"
#include <stddef.h>  // For NULL

/*
 * Extract_String - Extract a string pointer from a string data block
 * 
 * This is a simplified implementation that assumes the data block
 * contains an array of offsets followed by the string data.
 */
char *Extract_String(void const *data, int string)
{
	unsigned short int const *ptr;
	
	if (!data || string < 0) {
		return NULL;
	}
	
	ptr = (unsigned short int const *)data;
	return (((char*)data) + ptr[string]);
}

