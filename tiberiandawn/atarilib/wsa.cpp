/*
 * wsa.cpp - Windows Sockets API functions for Atari ST/MiNT
 * 
 * This provides implementations for XOR delta functions
 */

#include "wsa.h"

/*=========================================================================*/
/* Apply_XOR_Delta -- Apply XOR delta data to a linear buffer              */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly       */
/*                                                                         */
/* Command format:                                                          */
/*   n = 0-127: SHORTDUMP - copy next n bytes, XORing each                 */
/*   n = 0: SHORTRUN - run of next byte, count from next byte              */
/*   n = 128-255: SHORTSKIP - skip n-128 bytes                             */
/*   n = 128, w = word: LONGSKIP - skip w bytes (if w > 0)                */
/*   n = 128, w = 0: STOP - end of data                                   */
/*   n = 128, w = 0x8000-0xBFFF: LONGRUN - run of next byte, count = w-0x8000 */
/*   n = 128, w = 0x4000-0x7FFF: LONGDUMP - copy w-0x4000 bytes, XORing    */
/*                                                                         */
/* INPUT:                                                                  */
/*   target -- Destination buffer                                         */
/*   delta  -- XOR delta data to apply                                     */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns number of bytes processed (or 0 on error)                    */
/*=========================================================================*/
extern "C" unsigned int Apply_XOR_Delta(char *target, char *delta)
{
	char *t = target;
	char *d = delta;
	unsigned int bytes_processed = 0;
	
	if (!target || !delta) {
		return 0;
	}
	
	while (1) {
		unsigned char code = (unsigned char)*d++;
		bytes_processed++;
		
		// Check for SHORTDUMP (0 < code < 128)
		if (code > 0 && code < 128) {
			// SHORTDUMP: copy next 'code' bytes, XORing each
			unsigned int count = code;
			for (unsigned int i = 0; i < count; i++) {
				*t++ ^= *d++;
				bytes_processed++;
			}
			continue;
		}
		
		// Check for SHORTRUN (code == 0)
		if (code == 0) {
			// SHORTRUN: run of next byte, count from next byte
			unsigned char count = (unsigned char)*d++;
			unsigned char value = *d++;
			bytes_processed += 2;
			
			for (unsigned int i = 0; i < count; i++) {
				*t++ ^= value;
			}
			continue;
		}
		
		// Check for SHORTSKIP (128 <= code < 256, code != 128)
		if (code > 128) {
			// SHORTSKIP: skip (code - 128) bytes
			unsigned int skip = code - 128;
			t += skip;
			continue;
		}
		
		// code == 128: get next word
		unsigned short word_code = *((unsigned short *)d);
		d += 2;
		bytes_processed += 2;
		
		// Check for STOP (word_code == 0)
		if (word_code == 0) {
			break;
		}
		
		// Check for LONGSKIP (word_code > 0)
		if (word_code > 0 && word_code < 0x8000) {
			// LONGSKIP: skip word_code bytes
			t += word_code;
			continue;
		}
		
		// Check for LONGRUN (0x8000 <= word_code < 0xC000)
		if (word_code >= 0x8000 && word_code < 0xC000) {
			// LONGRUN: run of next byte, count = word_code - 0x8000
			unsigned int count = word_code - 0x8000;
			unsigned char value = *d++;
			bytes_processed++;
			
			for (unsigned int i = 0; i < count; i++) {
				*t++ ^= value;
			}
			continue;
		}
		
		// Check for LONGDUMP (0x4000 <= word_code < 0x8000)
		if (word_code >= 0x4000 && word_code < 0x8000) {
			// LONGDUMP: copy (word_code - 0x4000) bytes, XORing each
			unsigned int count = word_code - 0x4000;
			for (unsigned int i = 0; i < count; i++) {
				*t++ ^= *d++;
				bytes_processed++;
			}
			continue;
		}
		
		// Invalid code - break to avoid infinite loop
		break;
	}
	
	return bytes_processed;
}

/*=========================================================================*/
/* Apply_XOR_Delta_To_Page_Or_Viewport -- Apply XOR delta to page/viewport */
/*                                                                         */
/* Stub implementation - not fully implemented yet                        */
/*=========================================================================*/
extern "C" void Apply_XOR_Delta_To_Page_Or_Viewport(void *target, void *delta, int width, int nextrow, int copy)
{
	// Stub implementation
	// A full implementation would handle page/viewport boundaries
	(void)width;
	(void)nextrow;
	(void)copy;
	
	if (copy) {
		// Copy mode - just copy delta data
		// Not implemented yet
	} else {
		// XOR mode - apply XOR delta
		Apply_XOR_Delta((char *)target, (char *)delta);
	}
}

