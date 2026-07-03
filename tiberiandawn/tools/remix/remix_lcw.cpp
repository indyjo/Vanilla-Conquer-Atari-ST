/*
 * Bounded LCW decompress — remix-local port of common/lcw.cpp for WASM-safe decoding.
 */

#include "remix_decode.h"

#include <stddef.h>

static int remix_src_byte(const unsigned char **source_ptr, const unsigned char *source_end)
{
	if (*source_ptr >= source_end)
		return -1;
	return (int)(*(*source_ptr)++);
}

int remix_lcw_uncompress(
    const void *source, size_t source_len, void *dest, unsigned dest_len)
{
	const unsigned char *source_ptr;
	const unsigned char *source_end;
	unsigned char *dest_ptr;
	unsigned char *dest_end;
	unsigned char *copy_ptr;
	unsigned op_code;
	unsigned count;
	int b;

	if (!source || !dest || dest_len == 0)
		return -1;

	source_ptr = (const unsigned char *)source;
	source_end = source_ptr + source_len;
	dest_ptr = (unsigned char *)dest;
	dest_end = dest_ptr + dest_len;

	while (dest_ptr < dest_end) {
		b = remix_src_byte(&source_ptr, source_end);
		if (b < 0)
			return -1;
		op_code = (unsigned)b;

		if (!(op_code & 0x80)) {
			unsigned back;

			count = (op_code >> 4) + 3u;
			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			back = (unsigned)b + (((unsigned)op_code & 0x0fu) << 8);
			if (back >= (unsigned)(dest_ptr - (unsigned char *)dest))
				return -1;
			copy_ptr = dest_ptr - back;

			if (count > (unsigned)(dest_end - dest_ptr))
				count = (unsigned)(dest_end - dest_ptr);

			while (count--) {
				if (copy_ptr < (unsigned char *)dest || copy_ptr >= dest_end)
					return -1;
				*dest_ptr++ = *copy_ptr++;
			}
		} else if (!(op_code & 0x40)) {
			if (op_code == 0x80)
				return (int)(dest_ptr - (unsigned char *)dest);

			count = op_code & 0x3fu;
			if (count > (unsigned)(dest_end - dest_ptr))
				count = (unsigned)(dest_end - dest_ptr);

			while (count--) {
				b = remix_src_byte(&source_ptr, source_end);
				if (b < 0)
					return -1;
				*dest_ptr++ = (unsigned char)b;
			}
		} else if (op_code == 0xfe) {
			unsigned run_byte;

			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			count = (unsigned)b;
			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			count += (unsigned)b << 8u;

			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			run_byte = (unsigned)b;

			if (count > (unsigned)(dest_end - dest_ptr))
				count = (unsigned)(dest_end - dest_ptr);

			while (count--)
				*dest_ptr++ = (unsigned char)run_byte;
		} else if (op_code == 0xff) {
			unsigned off_lo;
			unsigned off_hi;

			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			count = (unsigned)b;
			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			count += (unsigned)b << 8u;

			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			off_lo = (unsigned)b;
			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			off_hi = (unsigned)b;

			copy_ptr = (unsigned char *)dest + off_lo + (off_hi << 8u);
			if (count > (unsigned)(dest_end - dest_ptr))
				count = (unsigned)(dest_end - dest_ptr);

			while (count--) {
				if (copy_ptr < (unsigned char *)dest || copy_ptr >= dest_end)
					return -1;
				*dest_ptr++ = *copy_ptr++;
			}
		} else {
			unsigned off_lo;
			unsigned off_hi;

			count = (op_code & 0x3fu) + 3u;
			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			off_lo = (unsigned)b;
			b = remix_src_byte(&source_ptr, source_end);
			if (b < 0)
				return -1;
			off_hi = (unsigned)b;

			copy_ptr = (unsigned char *)dest + off_lo + (off_hi << 8u);
			if (count > (unsigned)(dest_end - dest_ptr))
				count = (unsigned)(dest_end - dest_ptr);

			while (count--) {
				if (copy_ptr < (unsigned char *)dest || copy_ptr >= dest_end)
					return -1;
				*dest_ptr++ = *copy_ptr++;
			}
		}
	}

	return (int)(dest_ptr - (unsigned char *)dest);
}
