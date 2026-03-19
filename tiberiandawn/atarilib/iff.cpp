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
/* LCW_Uncompress -- Westwood LCW ("Format 80") decompression              */
/*                                                                         */
/* Command format (absolute mode):                                         */
/*  - cmd1: 10cccccc : copy next c bytes (c==0 => end marker)              */
/*  - cmd2: 0ccc pppp + pp : copy (c+3) bytes from (dst - pos)             */
/*  - cmd3: 11cccccc + word pos : copy (c+3) bytes from (dst0 + pos)       */
/*  - cmd4: 0xFE + word count + byte value : fill                          */
/*  - cmd5: 0xFF + word count + word pos : long copy from (dst0 + pos)     */
/* Relative mode: if stream starts with byte 0, cmd3/cmd5 pos are relative */
/* (dst - pos) instead of absolute from dst0.                              */
/*=========================================================================*/
static inline unsigned short read_lcw_u16(const unsigned char *p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

extern "C" unsigned long LCW_Uncompress(void *source, void *dest, unsigned long length)
{
	if (!source || !dest || length == 0) return 0;

	const unsigned char *src = (const unsigned char *)source;
	unsigned char *dst = (unsigned char *)dest;
	unsigned char *dst0 = dst;
	unsigned char *dst_end = dst + length;

	bool relative_mode = false;
	if (*src == 0) {
		relative_mode = true;
		src++;
	}

	while (dst < dst_end) {
		unsigned char code = *src++;

		/* cmd2: existing block relative copy (2 bytes) */
		if ((code & 0x80) == 0) {
			unsigned long count = (unsigned long)((code & 0x70) >> 4) + 3UL;
			unsigned long pos = ((unsigned long)(code & 0x0F) << 8) | (unsigned long)(*src++);
			unsigned char *from = dst - (long)pos;
			if (from < dst0) break;
			if (dst + count > dst_end) count = (unsigned long)(dst_end - dst);
			/*
			** Important: LCW backreferences behave like LZ77 forward copies where
			** newly written bytes can be referenced during the same copy command.
			** Using memmove() can produce different results when src/dst overlap.
			** Copy byte-by-byte in forward order to match LZ semantics.
			*/
			for (unsigned long k = 0; k < count; k++) {
				*dst++ = from[k];
			}
			continue;
		}

		/* cmd1: literal copy (or end marker 0x80) */
		if ((code & 0x40) == 0) {
			unsigned long count = (unsigned long)(code & 0x3F);
			if (count == 0) break; /* end marker */
			if (dst + count > dst_end) count = (unsigned long)(dst_end - dst);
			memcpy(dst, src, count);
			dst += count;
			src += count;
			continue;
		}

		/* cmd4: fill */
		if (code == 0xFE) {
			unsigned long count = (unsigned long)read_lcw_u16(src);
			src += 2;
			unsigned char value = *src++;
			if (dst + count > dst_end) count = (unsigned long)(dst_end - dst);
			memset(dst, value, count);
			dst += count;
			continue;
		}

		/* cmd5: long copy */
		if (code == 0xFF) {
			unsigned long count = (unsigned long)read_lcw_u16(src);
			src += 2;
			unsigned short posw = read_lcw_u16(src);
			src += 2;
			unsigned char *from = relative_mode ? (dst - (long)posw) : (dst0 + (long)posw);
			if (from < dst0) break;
			if (dst + count > dst_end) count = (unsigned long)(dst_end - dst);
			for (unsigned long k = 0; k < count; k++) {
				*dst++ = from[k];
			}
			continue;
		}

		/* cmd3: medium-length copy */
		{
			unsigned long count = (unsigned long)(code & 0x3F) + 3UL;
			unsigned short posw = read_lcw_u16(src);
			src += 2;
			unsigned char *from = relative_mode ? (dst - (long)posw) : (dst0 + (long)posw);
			if (from < dst0) break;
			if (dst + count > dst_end) count = (unsigned long)(dst_end - dst);
			for (unsigned long k = 0; k < count; k++) {
				*dst++ = from[k];
			}
			continue;
		}
	}

	return (unsigned long)(dst - dst0);
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

