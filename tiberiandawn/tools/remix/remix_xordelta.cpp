/*
 * Bounded XOR delta apply — remix-local port of common/xordelta.cpp for WASM-safe decoding.
 */

#include "remix_decode.h"

#include <stddef.h>

static int remix_xor_src_byte(const unsigned char **getp, const unsigned char *end)
{
	if (*getp >= end)
		return -1;
	return (int)(*(*getp)++);
}

int remix_xor_delta_apply(void *dst, size_t dst_len, const void *src, size_t src_len)
{
	unsigned char *putp;
	unsigned char *put_end;
	const unsigned char *getp;
	const unsigned char *get_end;
	unsigned char value;
	unsigned char cmd;
	unsigned short count;
	int b;
	int xorval;

	if (!dst || !src || dst_len == 0 || src_len == 0)
		return 0;

	putp = (unsigned char *)dst;
	put_end = putp + dst_len;
	getp = (const unsigned char *)src;
	get_end = getp + src_len;

	while (1) {
		xorval = 0;
		b = remix_xor_src_byte(&getp, get_end);
		if (b < 0)
			return 0;
		cmd = (unsigned char)b;
		count = cmd;

		if (!(cmd & 0x80)) {
			if (cmd == 0) {
				b = remix_xor_src_byte(&getp, get_end);
				if (b < 0)
					return 0;
				count = (unsigned short)(unsigned char)b;
				b = remix_xor_src_byte(&getp, get_end);
				if (b < 0)
					return 0;
				value = (unsigned char)b;
				xorval = 1;
			}
		} else {
			count = (unsigned short)(cmd & 0x7fu);
			if (count != 0) {
				if ((size_t)count > (size_t)(put_end - putp))
					return 0;
				putp += count;
				continue;
			}

			b = remix_xor_src_byte(&getp, get_end);
			if (b < 0)
				return 0;
			count = (unsigned short)(unsigned char)b;
			b = remix_xor_src_byte(&getp, get_end);
			if (b < 0)
				return 0;
			count = (unsigned short)(count + ((unsigned short)(unsigned char)b << 8));

			if (count == 0)
				return 1;

			if ((count & 0x8000u) == 0) {
				if ((size_t)count > (size_t)(put_end - putp))
					return 0;
				putp += count;
				continue;
			}

			if (count & 0x4000u) {
				count = (unsigned short)(count & 0x3fffu);
				b = remix_xor_src_byte(&getp, get_end);
				if (b < 0)
					return 0;
				value = (unsigned char)b;
				xorval = 1;
			} else {
				count = (unsigned short)(count & 0x3fffu);
			}
		}

		if (xorval) {
			for (; count > 0; --count) {
				if (putp >= put_end)
					return 0;
				*putp++ ^= value;
			}
		} else {
			for (; count > 0; --count) {
				if (putp >= put_end)
					return 0;
				b = remix_xor_src_byte(&getp, get_end);
				if (b < 0)
					return 0;
				*putp++ ^= (unsigned char)b;
			}
		}
	}
}
