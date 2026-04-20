#include "ws_adpcm_68k.h"

namespace {

static short const kImaStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
	1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
	8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

static signed char const kImaIndexTable[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static int g_inited;
static int g_delta_table[89 * 16];

static inline int clamp16(int x)
{
	if (x > 32767) {
		return 32767;
	}
	if (x < -32768) {
		return -32768;
	}
	return x;
}

static inline int clamp_step(int x)
{
	if (x > 88) {
		return 88;
	}
	if (x < 0) {
		return 0;
	}
	return x;
}

/*
 * Decode one packed byte -> two nibble codes.
 * Kept as GNU-style inline asm on m68k to mirror the original loop shape.
 */
static inline void unpack_nibbles(unsigned char b, unsigned* lo, unsigned* hi)
{
#if defined(__GNUC__) && defined(__m68k__)
	unsigned dlo = 0;
	unsigned dhi = 0;
	asm volatile(
	    "moveq #0,%0\n\t"
	    "moveq #0,%1\n\t"
	    "move.b %2,%0\n\t"
	    "move.b %0,%1\n\t"
	    "and.b #15,%1\n\t"
	    "lsr.b #4,%0\n\t"
	    : "=&d"(dhi), "=&d"(dlo)
	    : "d"(b)
	    : "cc");
	*lo = dlo;
	*hi = dhi;
#else
	*lo = (unsigned)(b & 0x0F);
	*hi = (unsigned)(b >> 4);
#endif
}

} // namespace

void ws_adpcm68k_init_tables(void)
{
	if (g_inited) {
		return;
	}
	for (int si = 0; si < 89; ++si) {
		int const step = (int)kImaStepTable[si];
		for (int nib = 0; nib < 16; ++nib) {
			int const delta = nib & 7;
			int diff = ((2 * delta + 1) * step) >> 3; /* Westwood IMA shift=3 */
			if (nib & 8) {
				diff = -diff;
			}
			g_delta_table[si * 16 + nib] = diff;
		}
	}
	g_inited = 1;
}

void ws_adpcm68k_decode_mono16(
    struct WsAdpcm68kState* st, unsigned char const* src, unsigned src_bytes, short* dst, unsigned dst_samples)
{
	if (!st || !src || !dst || src_bytes == 0 || dst_samples == 0) {
		return;
	}
	if (!g_inited) {
		ws_adpcm68k_init_tables();
	}

	int pred = st->predictor;
	int si = clamp_step(st->step_index);
	unsigned outp = 0;

	for (unsigned i = 0; i < src_bytes && outp < dst_samples; ++i) {
		unsigned lo = 0;
		unsigned hi = 0;
		unpack_nibbles(src[i], &lo, &hi);

		pred += g_delta_table[si * 16 + (int)lo];
		pred = clamp16(pred);
		si = clamp_step(si + (int)kImaIndexTable[lo]);
		dst[outp++] = (short)pred;

		if (outp >= dst_samples) {
			break;
		}

		pred += g_delta_table[si * 16 + (int)hi];
		pred = clamp16(pred);
		si = clamp_step(si + (int)kImaIndexTable[hi]);
		dst[outp++] = (short)pred;
	}

	st->predictor = pred;
	st->step_index = si;
}

void ws_adpcm68k_build_volume_lut(signed char lut[256], int volume)
{
	if (!lut) {
		return;
	}
	if (volume < 0) {
		volume = 0;
	} else if (volume > 0xFF) {
		volume = 0xFF;
	}
	/*
	 * Entry i encodes the signed 8-bit predictor value (i - 128) attenuated by `volume`, where
	 * (s8 * volume) >> 8 is the same formula the old per-sample multiply used. volume=0xFF
	 * loses ~0.4% (inaudible) and avoids a per-sample divide.
	 */
	for (int i = 0; i < 256; ++i) {
		int const s = i - 128;
		lut[i] = (signed char)((s * volume) >> 8);
	}
}

void ws_adpcm68k_decode_mono8(
    struct WsAdpcm68kState* st, unsigned char const* src, unsigned src_bytes,
    signed char* dst, unsigned dst_samples, signed char const lut[256])
{
	if (!st || !src || !dst || !lut || src_bytes == 0 || dst_samples == 0) {
		return;
	}
	if (!g_inited) {
		ws_adpcm68k_init_tables();
	}

	int pred = st->predictor;
	int si = clamp_step(st->step_index);
	unsigned outp = 0;

	for (unsigned i = 0; i < src_bytes && outp < dst_samples; ++i) {
		unsigned lo = 0;
		unsigned hi = 0;
		unpack_nibbles(src[i], &lo, &hi);

		pred += g_delta_table[si * 16 + (int)lo];
		pred = clamp16(pred);
		si = clamp_step(si + (int)kImaIndexTable[lo]);
		/*
		 * `(pred >> 8) + 128` maps [-32768, 32767] -> [0, 255] after arithmetic shift. The LUT
		 * replaces an int*int multiply (which traps to __mulsi3 on 68000) with a single byte
		 * load, keeping per-sample cost deterministic and cheap.
		 */
		dst[outp++] = lut[(unsigned char)((pred >> 8) + 128)];

		if (outp >= dst_samples) {
			break;
		}

		pred += g_delta_table[si * 16 + (int)hi];
		pred = clamp16(pred);
		si = clamp_step(si + (int)kImaIndexTable[hi]);
		dst[outp++] = lut[(unsigned char)((pred >> 8) + 128)];
	}

	st->predictor = pred;
	st->step_index = si;
}
