/*
 * c2p.cpp - Chunky-to-planar conversion for Atari ST LoRes
 */

#include "c2p.h"
#include <string.h>    /* memset */

/* 4x4 Bayer threshold matrix, values 0..15 */
static const uint8_t Bayer4x4[16] = {
	0,  8,  2, 10,
	12, 4, 14, 6,
	3, 11, 1,  9,
	15, 7, 13, 5
};

#include "c2p_palette_opt_weights.inc"
#define kC2PPaletteOptWeight kC2PPaletteOptWeightHTitle
#include "c2p_palette_opt_weights_htitle.inc"
#undef kC2PPaletteOptWeight

/*
** Palette+dither dependent map:
**   index = ((y&3)<<2) | (x&3)  in [0..15]
**   src   = 8-bit palette index
**   value = 4-bit ST color (0..15)
**
** Filled from palette-opt (TEMPERAT.PAL, subset 0..15); see kC2PPaletteOptWeight.
*/
static uint8_t C2P_MapDither[16][256];

/* Pair LUTs: 4 pair positions (pixels 0-1,2-3,4-5,6-7) and two-nibble index. */
static uint32_t C2P_PairLUT[4][256];
static int C2P_LUT_InitDone = 0;
static int C2P_WeightSet = C2P_WEIGHTSET_TEMPERAT;

static const uint8_t (*C2P_ActivePaletteWeights)[16] = kC2PPaletteOptWeight;

/* Map 8-bit logical color to ST index 0..15 using palette-opt weights + Bayer rank. */
static uint8_t C2P_STIndex_FromOptWeights(int x_mod4, int y_mod4, uint8_t src_idx)
{
	const int b = (y_mod4 << 2) | x_mod4;
	const int rank = (int)Bayer4x4[b];
	const uint8_t *w = C2P_ActivePaletteWeights[src_idx];
	int cum = 0;
	for (int k = 0; k < 16; k++) {
		cum += (int)w[k];
		if (rank < cum)
			return (uint8_t)k;
	}
	return 15;
}

static void C2P_InitPairLUT_Once(void)
{
	if (C2P_LUT_InitDone)
		return;

	for (int pair = 0; pair < 4; pair++) {
		const int bit0 = 7 - (pair * 2 + 0);
		const int bit1 = 7 - (pair * 2 + 1);
		const uint8_t m0 = (uint8_t)(1u << bit0);
		const uint8_t m1 = (uint8_t)(1u << bit1);

		for (int idx = 0; idx < 256; idx++) {
			const uint8_t c0 = (uint8_t)((idx >> 4) & 0x0F);
			const uint8_t c1 = (uint8_t)(idx & 0x0F);

			uint8_t p0 = 0, p1 = 0, p2 = 0, p3 = 0;

			/* pixel 0 */
			if (c0 & 0x1) p0 |= m0;
			if (c0 & 0x2) p1 |= m0;
			if (c0 & 0x4) p2 |= m0;
			if (c0 & 0x8) p3 |= m0;

			/* pixel 1 */
			if (c1 & 0x1) p0 |= m1;
			if (c1 & 0x2) p1 |= m1;
			if (c1 & 0x4) p2 |= m1;
			if (c1 & 0x8) p3 |= m1;

			C2P_PairLUT[pair][idx] =
				((uint32_t)p0 << 24) |
				((uint32_t)p1 << 16) |
				((uint32_t)p2 <<  8) |
				((uint32_t)p3 <<  0);
		}
	}

	C2P_LUT_InitDone = 1;
}

extern "C" unsigned char C2P_Map8ToPlanar4(int abs_x, int abs_y, unsigned char pal_idx)
{
	if (!C2P_LUT_InitDone)
		C2P_Rebuild_Tables_From_CurrentPalette();
	const int yb = (abs_y & 3) << 2;
	return C2P_MapDither[yb | (abs_x & 3)][pal_idx];
}

extern "C" void C2P_Rebuild_Tables_From_CurrentPalette(void)
{
	C2P_InitPairLUT_Once();

	for (int b = 0; b < 16; b++) {
		const int xb = b & 3;
		const int yb = b >> 2;
		for (int src = 0; src < 256; src++) {
			C2P_MapDither[b][src] = C2P_STIndex_FromOptWeights(xb, yb, (uint8_t)src);
		}
	}
}

extern "C" int C2P_Get_WeightSet(void)
{
	return C2P_WeightSet;
}

extern "C" void C2P_Select_WeightSet(int weight_set)
{
	const int normalized = (weight_set == C2P_WEIGHTSET_HTITLE) ? C2P_WEIGHTSET_HTITLE : C2P_WEIGHTSET_TEMPERAT;
	if (normalized == C2P_WeightSet)
		return;

	C2P_WeightSet = normalized;
	C2P_ActivePaletteWeights = (C2P_WeightSet == C2P_WEIGHTSET_HTITLE) ? kC2PPaletteOptWeightHTitle : kC2PPaletteOptWeight;
	if (C2P_LUT_InitDone)
		C2P_Rebuild_Tables_From_CurrentPalette();
}

/* movep.l d0,(a0) writes bytes to 0,2,4,6(a0): perfect for plane bytes. */
static inline void C2P_Movep_Store(uint8_t *dst_plane_bytes, uint32_t plane_bytes)
{
#if defined(__m68k__)
	__asm__ volatile(
		"movep.l %0,0(%1)"
		:
		: "d"(plane_bytes), "a"(dst_plane_bytes)
		: "memory");
#else
	/* Non-m68k fallback: write the bytes explicitly with the same spacing. */
	dst_plane_bytes[0] = (uint8_t)(plane_bytes >> 24);
	dst_plane_bytes[2] = (uint8_t)(plane_bytes >> 16);
	dst_plane_bytes[4] = (uint8_t)(plane_bytes >> 8);
	dst_plane_bytes[6] = (uint8_t)(plane_bytes >> 0);
#endif
}

extern "C" void C2P_Render_Logical_To_ST_Screen(const uint8_t *logical, int logical_stride, uint8_t *st_screen)
{
	if (!logical || !st_screen || logical_stride <= 0)
		return;

	/* LoRes mode */
	const int screen_width = 320;
	const int screen_height = 200;
	const int bytes_per_line = 160; /* 20 groups * 8 bytes */

	/* Ensure tables exist even if caller forgot to rebuild. */
	if (!C2P_LUT_InitDone) {
		C2P_Rebuild_Tables_From_CurrentPalette();
	}

	for (int y = 0; y < screen_height; y++) {
		const uint8_t *src = logical + y * logical_stride;
		uint8_t *dst_line = st_screen + y * bytes_per_line;

		const int yb = (y & 3) << 2;

		/* Process 8 pixels at a time: two writes per 16-pixel ST group. */
		for (int x = 0; x < screen_width; x += 8) {
			const int group = x >> 4;              /* 0..19 */
			const int half = (x >> 3) & 1;         /* 0 for pixels 0..7, 1 for 8..15 */
			uint8_t *dst = dst_line + group * 8 + half;

			/* Map+ dither: 8 chunky palette indices -> 8 ST 4-bit colors. */
			const uint8_t c0 = C2P_MapDither[yb | ((x + 0) & 3)][src[x + 0]];
			const uint8_t c1 = C2P_MapDither[yb | ((x + 1) & 3)][src[x + 1]];
			const uint8_t c2 = C2P_MapDither[yb | ((x + 2) & 3)][src[x + 2]];
			const uint8_t c3 = C2P_MapDither[yb | ((x + 3) & 3)][src[x + 3]];
			const uint8_t c4 = C2P_MapDither[yb | ((x + 4) & 3)][src[x + 4]];
			const uint8_t c5 = C2P_MapDither[yb | ((x + 5) & 3)][src[x + 5]];
			const uint8_t c6 = C2P_MapDither[yb | ((x + 6) & 3)][src[x + 6]];
			const uint8_t c7 = C2P_MapDither[yb | ((x + 7) & 3)][src[x + 7]];

			/* Table-based pack: 4 pair LUTs -> one 32-bit register with plane bytes. */
			const uint32_t v =
				C2P_PairLUT[0][(uint8_t)((c0 << 4) | c1)] |
				C2P_PairLUT[1][(uint8_t)((c2 << 4) | c3)] |
				C2P_PairLUT[2][(uint8_t)((c4 << 4) | c5)] |
				C2P_PairLUT[3][(uint8_t)((c6 << 4) | c7)];

			C2P_Movep_Store(dst, v);
		}
	}
}

static inline uint8_t *ST_ChunkPtr(uint8_t *base, int x, int y)
{
	return base + y * ST_PLANAR_BYTES_PER_LINE + (x >> 4) * 8 + ((x >> 3) & 1);
}

extern "C" void ST_Planar_PutPixel(uint8_t *base, int x, int y, unsigned char color4)
{
	if (!base || x < 0 || x >= ST_PLANAR_WIDTH || y < 0 || y >= ST_PLANAR_HEIGHT)
		return;
	uint8_t *p = ST_ChunkPtr(base, x, y);
	const int bitnum = 7 - (x & 7);
	const uint8_t mask = (uint8_t)(1u << bitnum);
	const uint8_t c = (uint8_t)(color4 & 15);
	for (int pl = 0; pl < 4; pl++) {
		uint8_t *pb = p + pl * 2;
		if (c & (uint8_t)(1u << pl))
			*pb |= mask;
		else
			*pb &= (uint8_t)~mask;
	}
}

extern "C" unsigned char ST_Planar_GetPixel(const uint8_t *base, int x, int y)
{
	if (!base || x < 0 || x >= ST_PLANAR_WIDTH || y < 0 || y >= ST_PLANAR_HEIGHT)
		return 0;
	const uint8_t *p = ST_ChunkPtr((uint8_t *)base, x, y);
	const int bitnum = 7 - (x & 7);
	const uint8_t mask = (uint8_t)(1u << bitnum);
	uint8_t c = 0;
	for (int pl = 0; pl < 4; pl++) {
		if (p[pl * 2] & mask)
			c |= (uint8_t)(1u << pl);
	}
	return c;
}

extern "C" void ST_Planar_Clear(uint8_t *base, unsigned char color4)
{
	if (!base)
		return;
	const uint8_t c = (uint8_t)(color4 & 15);
	/*
	 * Avoid libc memset for ST screen clears here.
	 * On-target diagnostics show font data adjacent to the planar buffer being clobbered
	 * after Clear(0); using the explicit planar store loop keeps writes confined to the
	 * exact ST interleaved layout (200 lines * 160 bytes).
	 */
	C2P_InitPairLUT_Once();
	const uint8_t pair_idx = (uint8_t)((c << 4) | c);
	const uint32_t v =
		C2P_PairLUT[0][pair_idx] |
		C2P_PairLUT[1][pair_idx] |
		C2P_PairLUT[2][pair_idx] |
		C2P_PairLUT[3][pair_idx];
	for (int y = 0; y < ST_PLANAR_HEIGHT; y++) {
		uint8_t *dst_line = base + y * ST_PLANAR_BYTES_PER_LINE;
		for (int x = 0; x < ST_PLANAR_WIDTH; x += 8) {
			const int group = x >> 4;
			const int half = (x >> 3) & 1;
			uint8_t *dst = dst_line + group * 8 + half;
			C2P_Movep_Store(dst, v);
		}
	}
}

extern "C" void C2P_Blit_Linear8_To_Planar(
	uint8_t *planar_base,
	int dst_x, int dst_y,
	const uint8_t *src, int w, int h, int src_stride,
	int trans)
{
	if (!planar_base || !src || w <= 0 || h <= 0 || src_stride <= 0)
		return;
	if (!C2P_LUT_InitDone)
		C2P_Rebuild_Tables_From_CurrentPalette();

	for (int yy = 0; yy < h; yy++) {
		const int py = dst_y + yy;
		if (py < 0 || py >= ST_PLANAR_HEIGHT)
			continue;
		const int yb = (py & 3) << 2;
		const uint8_t *srcrow = src + (size_t)yy * (size_t)src_stride;

		for (int xx = 0; xx < w; xx++) {
			const int px = dst_x + xx;
			if (px < 0 || px >= ST_PLANAR_WIDTH)
				continue;
			const uint8_t sp = srcrow[xx];
			if (trans && sp == 0)
				continue;
			const uint8_t c4 = C2P_MapDither[yb | (px & 3)][sp];
			ST_Planar_PutPixel(planar_base, px, py, c4);
		}
	}
}

