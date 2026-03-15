/*
 * iff.cpp - IFF file format functions for Atari ST/MiNT
 */

#include "iff.h"
#include "windows.h"  // For BOOL type
#include "misc.h"
#include <string.h>   // memcpy, memmove, memset

/* Compressed block header is always 8 bytes and little-endian in file. */
#define COMP_HEADER_SIZE 8

static inline unsigned long ReadLE32_iff(const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
		((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static inline unsigned short ReadLE16_iff(const unsigned char *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

/*=========================================================================*/
/* Uncompress_Data -- Uncompresses data from one buffer to another        */
/*                                                                         */
/* This is a portable C implementation matching WIN32LIB/load.cpp        */
/*                                                                         */
/* INPUT:                                                                  */
/*   src   -- Source compressed data pointer                             */
/*   dst   -- Destination pointer                                        */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns with the size of the uncompressed data                        */
/*=========================================================================*/

/*=========================================================================*/
/* RLE_Uncompress -- Run-Length Encoding decompression                     */
/*                                                                         */
/* This is a portable C implementation of the RLE algorithm              */
/* Format: If byte > 192, it's a run: (byte-192) copies of next byte      */
/*         Otherwise, it's a literal byte                                 */
/* The size parameter is the uncompressed size (destination size)        */
/*=========================================================================*/
extern "C" void RLE_Uncompress(void *src, void *dst, unsigned long size)
{
	unsigned char *s = (unsigned char *)src;
	unsigned char *d = (unsigned char *)dst;
	unsigned char *d_end = d + size;
	
	while (d < d_end) {
		unsigned char code = *s++;
		
		if (code > 192) {
			// Run: (code-192) copies of next byte
			unsigned long run_length = code - 192;
			unsigned char value = *s++;
			
			// Clamp run length to remaining space
			unsigned long remaining = d_end - d;
			if (run_length > remaining) {
				run_length = remaining;
			}
			memset(d, value, run_length);
			d += run_length;
		} else {
			// Literal byte
			if (d < d_end) {
				*d++ = code;
			} else {
				break;
			}
		}
	}
}

/*=========================================================================*/
/* LCW_Uncompress -- Westwood LCW (Lempel-Ziv-Westwood) decompression      */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly       */
/* Command format:                                                          */
/*   n=0xxxyyyy,yyyyyyyy     short run: back y bytes, run x+3             */
/*   n=10xxxxxx,n1...nx+1    med length: copy next x+1 bytes               */
/*   n=11xxxxxx,w1           med run: run x+3 bytes from offset w1        */
/*   n=11111111,w1,w2        long copy: copy w1 bytes from offset w2      */
/*   n=11111110,w1,b1        long run: run byte b1 for w1 bytes           */
/*   n=10000000              end marker                                   */
/*=========================================================================*/
/* Read little-endian word without alignment requirement (68000-safe).   */
static inline unsigned short read_lcw_u16(const unsigned char *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

extern "C" unsigned long LCW_Uncompress(void *source, void *dest, unsigned long length)
{
	if (!source || !dest || length == 0)
		return 0;

	unsigned char *src = (unsigned char *)source;
	unsigned char *dst = (unsigned char *)dest;
	unsigned char *a1stdest = dst;
	unsigned char *lastbyte = dst + length;
	unsigned char *src_save = src;

	while (dst < lastbyte) {
		unsigned long maxlen = lastbyte - dst;
		src = src_save;

		unsigned char code = *src++;

		// Check for short run (bit 7 = 0)
		if ((code & 0x80) == 0) {
			// Short run: 0xxxyyyy format
			// Count = (code >> 4) + 3
			// Offset high = (code & 0x0F)
			unsigned long count = ((code >> 4) & 0x0F) + 3;
			unsigned char offset_high = code & 0x0F;

			// Clamp count
			if (count > maxlen) count = maxlen;

			// Get offset low byte
			unsigned char offset_low = *src++;
			src_save = src;

			// Calculate source position (relative to current dest)
			unsigned long offset = offset_low | (offset_high << 8);
			unsigned char *src_ptr = dst - offset;

			// Never read before start of buffer (malformed data / 68000 safety)
			if (src_ptr < a1stdest) {
				unsigned long skip = (unsigned long)(a1stdest - src_ptr);
				if (skip >= count) continue;
				count -= skip;
				src_ptr = a1stdest;
			}

			// Copy the run (overlap possible: src_ptr is in output buffer)
			memmove(dst, src_ptr, count);
			dst += count;
			continue;
		}
		
		// Check for end marker
		if (code == 0x80) {
			break;
		}
		
		// Check if it's a length command (bit 6 = 0, bit 7 = 1)
		if ((code & 0x40) == 0) {
			// Medium length: 10xxxxxx format, copy next (code & 0x3F) + 1 bytes
			unsigned long count = (code & 0x3F) + 1;
			
			// Clamp count
			if (count > maxlen) count = maxlen;
			
			// Copy literal bytes (no overlap: src is compressed stream)
			memcpy(dst, src, count);
			dst += count;
			src += count;
			src_save = src;
			continue;
		}
		
		// Not a length command - could be med run, long copy, or long run
		unsigned long count = (code & 0x3F) + 3;
		
		// Check for long run (0xFE)
		if (code == 0xFE) {
			// Long run: w1 bytes of byte b1
			unsigned short run_length = read_lcw_u16(src);
			src += 2;
			unsigned char run_value = *src++;
			src_save = src;
			
			// Clamp run length
			if (run_length > maxlen) run_length = maxlen;
			
			// Fill with run value (optimized for large runs)
			if (run_length <= 32) {
				memset(dst, run_value, run_length);
				dst += run_length;
			} else {
				// Large run - use word/dword fills where possible
				unsigned long dword_value = (run_value << 24) | (run_value << 16) | (run_value << 8) | run_value;
				unsigned long dword_count = run_length / 4;
				unsigned long remainder = run_length % 4;
				
				// Align to 4-byte boundary if needed
				unsigned long align = ((unsigned long)dst) & 3;
				if (align > 0) {
					align = 4 - align;
					for (unsigned long i = 0; i < align && i < run_length; i++) {
						*dst++ = run_value;
					}
					run_length -= align;
					dword_count = run_length / 4;
					remainder = run_length % 4;
				}
				
				// Fill with dwords
				unsigned long *dst_dword = (unsigned long *)dst;
				for (unsigned long i = 0; i < dword_count; i++) {
					*dst_dword++ = dword_value;
				}
				dst = (unsigned char *)dst_dword;
				
				// Fill remainder
				for (unsigned long i = 0; i < remainder; i++) {
					*dst++ = run_value;
				}
			}
			continue;
		}
		
		// Check for long copy (0xFF)
		if (code == 0xFF) {
			// Long copy: w1 bytes from offset w2
			unsigned short copy_length = read_lcw_u16(src);
			src += 2;
			unsigned short offset = read_lcw_u16(src);
			src += 2;
			src_save = src;
			
			// Clamp copy length
			if (copy_length > maxlen) copy_length = maxlen;
			
			// Calculate source position (relative to a1stdest)
			unsigned char *src_ptr = a1stdest + offset;
			
			// Copy the data (src_ptr is in output buffer; overlap possible)
			unsigned char *dst_end = dst + copy_length - 1;
			if (src_ptr > dst_end || src_ptr + copy_length <= dst) {
				memcpy(dst, src_ptr, copy_length);
			} else {
				memmove(dst, src_ptr, copy_length);
			}
			dst += copy_length;
			continue;
		}
		
		// Medium run: 11xxxxxx format, run x+3 bytes from offset w1
		// Clamp count
		if (count > maxlen) count = maxlen;
		
		// Get offset word
		unsigned short offset = read_lcw_u16(src);
		src += 2;
		src_save = src;

		// Calculate source position (relative to a1stdest)
		unsigned char *src_ptr = a1stdest + offset;
		
		// Copy the run (src_ptr is in output buffer; overlap possible)
		unsigned char *dst_end = dst + count - 1;
		if (src_ptr > dst_end || src_ptr + count <= dst) {
			memcpy(dst, src_ptr, count);
		} else {
			memmove(dst, src_ptr, count);
		}
		dst += count;
	}
	
	return dst - a1stdest;
}

extern "C" unsigned long Uncompress_Data(void const *src, void *dst)
{
	unsigned int skip;			// Number of leading data to skip.
	CompressionType method;		// Compression method used.
	unsigned long uncomp_size = 0;

	if (!src || !dst) return(0);

	/*
	**	Read header from fixed 8-byte layout, little-endian (file format).
	**	Avoids struct padding and endianness issues on big-endian (m68k).
	*/
	const unsigned char *h = (const unsigned char *)src;
	method = (CompressionType)h[0];
	uncomp_size = ReadLE32_iff(h + 2);
	skip = ReadLE16_iff(h + 6);
	src = Add_Long_To_Pointer((void *)src, (long)COMP_HEADER_SIZE + (long)skip);

	switch (method) {

		default:
		case NOCOMPRESS:
			Mem_Copy((void *) src, dst, uncomp_size);
			break;

		case HORIZONTAL:
			RLE_Uncompress((void *) src, dst, uncomp_size);
			break;

		case LCW:
			LCW_Uncompress((void *) src, (void *) dst, (unsigned long) uncomp_size);
			break;

	}

	return(uncomp_size);
}

