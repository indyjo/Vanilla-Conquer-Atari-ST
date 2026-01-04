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

extern "C" long __cdecl Calculate_CRC(void *buffer, long length)
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

