/*
 * Westwood IMA ADPCM decoder for m68k (AUD99 / ADPCM_IMA_WS, shift=3).
 *
 * Decodes packed 4-bit nibbles (low then high per byte) into mono PCM using a
 * precomputed step_index x nibble delta table and per-stream predictor/step state.
 * The fast path (ws_adpcm68k_decode_mono8) emits signed 8-bit samples for STE DMA
 * mixing; ws_adpcm68k_decode_mono16 is the 16-bit reference path.
 *
 * Loop structure and table-driven nibbles were inspired by the original
 * adpcm-68k project: https://github.com/Kalmalyzer/adpcm-68k
 * (adapted here for Westwood's IMA variant, not classic MS IMA).
 */
#include "ws_adpcm_68k.h"

namespace {

#if defined(__GNUC__)
#define WSADPCM_AINLINE __attribute__((always_inline)) static inline
#else
#define WSADPCM_AINLINE static inline
#endif

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
 * After clamp16(pred), pred is in [-32768,32767]; arithmetic >> 8 yields [-128,127] on GCC/m68k.
 */
static inline signed char pred_clamped_to_s8(int pred)
{
	return (signed char)(pred >> 8);
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

WSADPCM_AINLINE void apply_nibble_mono8_store(unsigned nib, int& pred, int& si, signed char*& d)
{
	unsigned const n = nib & 15U;
	pred += g_delta_table[((unsigned)si << 4) + n];
	pred = clamp16(pred);
	si = clamp_step(si + (int)kImaIndexTable[n]);
	*d++ = pred_clamped_to_s8(pred);
}

WSADPCM_AINLINE void apply_nibble_mono16(unsigned nib, int& pred, int& si, unsigned& outp,
    short* dst, unsigned dst_samples)
{
	unsigned const n = nib & 15U;
	pred += g_delta_table[((unsigned)si << 4) + n];
	pred = clamp16(pred);
	si = clamp_step(si + (int)kImaIndexTable[n]);
	if (outp < dst_samples) {
		dst[outp++] = (short)pred;
	}
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
	int si = clamp_step((int)st->step_index);
	unsigned outp = 0;

	for (unsigned i = 0; i < src_bytes && outp < dst_samples; ++i) {
		unsigned lo = 0;
		unsigned hi = 0;
		unpack_nibbles(src[i], &lo, &hi);

		apply_nibble_mono16(lo, pred, si, outp, dst, dst_samples);
		if (outp >= dst_samples) {
			break;
		}

		apply_nibble_mono16(hi, pred, si, outp, dst, dst_samples);
	}

	st->predictor = pred;
	st->step_index = (short)si;
}

unsigned ws_adpcm68k_decode_mono8(
    struct WsAdpcm68kState* st, unsigned char const* src, unsigned src_bytes,
    signed char* dst, unsigned dst_samples, unsigned* out_src_bytes_used)
{
	if (out_src_bytes_used) {
		*out_src_bytes_used = 0;
	}
	if (!st || !dst || dst_samples == 0 || (dst_samples & 1U) != 0U) {
		return 0;
	}
	if (!src && src_bytes > 0) {
		return 0;
	}
	if (!g_inited) {
		ws_adpcm68k_init_tables();
	}

	unsigned nbytes = dst_samples >> 1;
	if (nbytes > src_bytes) {
		nbytes = src_bytes;
	}
	if (nbytes == 0) {
		return 0;
	}

	int pred = st->predictor;
	int si = clamp_step((int)st->step_index);
	signed char* d = dst;
	unsigned char const* s = src;
	unsigned p = nbytes;
	while (p--) {
		unsigned char const b = *s++;
		apply_nibble_mono8_store((unsigned)(b & 15U), pred, si, d);
		apply_nibble_mono8_store(((unsigned)b >> 4) & 15U, pred, si, d);
	}

	st->predictor = pred;
	st->step_index = (short)si;
	if (out_src_bytes_used) {
		*out_src_bytes_used = nbytes;
	}
	return nbytes << 1;
}
