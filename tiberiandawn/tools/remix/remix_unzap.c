#include "remix_unzap.h"

#include <string.h>

static const signed char kZapTabTwo[4] = {-2, -1, 0, 1};
static const signed char kZapTabFour[16] = {
	-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8
};

static int clamp_u8(int x)
{
	if (x > 255)
		return 255;
	if (x < 0)
		return 0;
	return x;
}

int remix_unzap_stream(
    const unsigned char *src, size_t src_len, size_t out_len,
    RemixUnzapWriteFn write_fn, void *ctx)
{
	int sample = 0x80;
	size_t src_pos = 0;
	size_t out_written = 0;

	if (!src || !write_fn || out_len == 0)
		return 0;

	while (out_written < out_len) {
		unsigned short shifted;
		unsigned char code;
		signed char count;

		if (src_pos >= src_len)
			return 0;

		shifted = (unsigned short)(src[src_pos++] << 2);
		code = (unsigned char)((shifted & 0xFF00u) >> 8);
		count = (signed char)((shifted & 0x00FFu) >> 2);

		switch (code) {
		case 2:
			if (count & 0x20) {
				count <<= 3;
				sample += count >> 3;
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
			} else {
				signed char n;
				for (n = (signed char)(count + 1); n > 0; --n) {
					if (src_pos >= src_len)
						return 0;
					if (!write_fn(ctx, src[src_pos++]))
						return 0;
					++out_written;
					if (out_written >= out_len)
						break;
				}
				if (src_pos > 0)
					sample = src[src_pos - 1];
			}
			break;

		case 1:
			for (count = (signed char)(count + 1); count > 0; --count) {
				if (src_pos >= src_len)
					return 0;
				code = src[src_pos++];
				sample += kZapTabFour[code & 0x0F];
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
				sample += kZapTabFour[code >> 4];
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
			}
			break;

		case 0:
			for (count = (signed char)(count + 1); count > 0; --count) {
				if (src_pos >= src_len)
					return 0;
				code = src[src_pos++];
				sample += kZapTabTwo[code & 0x03];
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
				sample += kZapTabTwo[(code >> 2) & 0x03];
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
				sample += kZapTabTwo[(code >> 4) & 0x03];
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
				sample += kZapTabTwo[(code >> 6) & 0x03];
				sample = clamp_u8(sample);
				if (!write_fn(ctx, (unsigned char)sample))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
			}
			break;

		default: {
			unsigned char fill = (unsigned char)clamp_u8(sample);
			signed char n;
			for (n = (signed char)(count + 1); n > 0; --n) {
				if (!write_fn(ctx, fill))
					return 0;
				++out_written;
				if (out_written >= out_len)
					break;
			}
			break;
		}
		}
	}

	return out_written == out_len;
}
