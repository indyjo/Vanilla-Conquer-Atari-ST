/*
 * c2p.cpp - Chunky-to-planar conversion for Atari ST LoRes
 */

#include "c2p.h"
#include "st_frame_meter.h"
#include <stddef.h>

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
uint8_t C2P_MapNearestLUT[256];
/* Bit 7: exactly one non-zero weight in row; bits 0..3: that ST color when set. */
uint8_t C2P_PaletteIndexClean4LUT[256];

/* Pair LUTs: 4 pair positions (pixels 0-1,2-3,4-5,6-7) and two-nibble index. */
static uint32_t C2P_PairLUT[4][256];
static int C2P_LUT_InitDone = 0;
static int C2P_WeightSet = C2P_WEIGHTSET_TEMPERAT;

/*
 * STDOOM-style full-width C2P (see STDOOM atari_c2p.c: bayer4_* + c2p_1x_lorez).
 * Fragment table: per scanline phase (y&3), palette index, pixel slot 0..7 within movep octet —
 * same Bayer + weight policy as init_c2p_table() lorez path in STDOOM.
 */
static uint32_t C2P_STDOOM_FragLUT[4][256][8];

/* STDOOM bayer4_color — threshold uses row `phase` (0..3) and column `px%4`. */
static int C2P_Bayer4_Color_Lorez(const uint8_t *weights, int phase, int px)
{
	unsigned char bayer_lwb = 0, bayer_upb = 0;
	for (int c = 0; c < 16; c++) {
		bayer_upb += weights[c];
		const int rank = (int)Bayer4x4[(phase << 2) | (px & 3)];
		if (rank >= bayer_lwb && rank < bayer_upb)
			return c;
		bayer_lwb += weights[c];
	}
	return 15;
}

/* One chunky pixel -> longword planar fragment for movep (STDOOM bayer4_lorez_pdata). */
static uint32_t C2P_Lorez_PData_FromWeights(const uint8_t *weights, int phase, int px)
{
	const int c = C2P_Bayer4_Color_Lorez(weights, phase, px);
	uint32_t pdata = 0;
	if (c & 1)
		pdata |= 0x01000000u;
	if (c & 2)
		pdata |= 0x00010000u;
	if (c & 4)
		pdata |= 0x00000100u;
	if (c & 8)
		pdata |= 0x00000001u;
	return pdata << (unsigned)(7 - px);
}

static void C2P_Rebuild_STDOOM_FragLUT(const uint8_t (*active_weights)[16])
{
	for (int pal = 0; pal < 256; pal++) {
		const uint8_t *weights = active_weights[pal];
		for (int phase = 0; phase < 4; phase++) {
			for (int px = 0; px < 8; px++)
				C2P_STDOOM_FragLUT[phase][pal][px] =
				    C2P_Lorez_PData_FromWeights(weights, phase, px);
		}
	}
}

/* movep.l d0,(a0) writes bytes to 0,2,4,6(a0): plane bytes for ST interleaved layout. */
static inline void C2P_Movep_Store(uint8_t *dst_plane_bytes, uint32_t plane_bytes)
{
#if defined(__m68k__)
	__asm__ volatile(
		"movep.l %0,0(%1)"
		:
		: "d"(plane_bytes), "a"(dst_plane_bytes)
		: "memory");
#else
	dst_plane_bytes[0] = (uint8_t)(plane_bytes >> 24);
	dst_plane_bytes[2] = (uint8_t)(plane_bytes >> 16);
	dst_plane_bytes[4] = (uint8_t)(plane_bytes >> 8);
	dst_plane_bytes[6] = (uint8_t)(plane_bytes >> 0);
#endif
}

/*
 * STDOOM c2p_1x_lorez — hand-tuned m68k inner loop; host fallback ORs the same fragments.
 */
#if defined(__m68k__)
static void C2P_1x_Lorez_STDOOM(uint8_t *out, const uint8_t *in, unsigned short pixels, uint32_t (*table)[8])
{
	if (pixels < 16)
		return;
	unsigned short groups = (unsigned short)(pixels / 16u - 1u);
	uint32_t pdata = 0;
	const uint32_t mask = 0x00ff00ffu << 5;
	__asm__ volatile(
		"0:                                         \n\t"
		"movem.l    (%[in])+, %%d0-%%d1             \n\t"
		"move.l     %%d0,%%d2                       \n\t"
		"lsl.l      #5,%%d2                         \n\t"
		"and.l      %[mask],%%d2                    \n\t"
		"move.l     12(%[table],%%d2.w), %[pdata]   \n\t"
		"swap       %%d2                            \n\t"
		"or.l       4(%[table],%%d2.w), %[pdata]    \n\t"
		"lsr.l      #3,%%d0                         \n\t"
		"and.l      %[mask],%%d0                    \n\t"
		"or.l       8(%[table],%%d0.w), %[pdata]    \n\t"
		"swap       %%d0                            \n\t"
		"or.l       (%[table],%%d0.w), %[pdata]     \n\t"
		"move.l     %%d1,%%d2                       \n\t"
		"lsl.l      #5,%%d2                         \n\t"
		"and.l      %[mask],%%d2                    \n\t"
		"or.l       28(%[table],%%d2.w), %[pdata]   \n\t"
		"swap       %%d2                            \n\t"
		"or.l       20(%[table],%%d2.w), %[pdata]   \n\t"
		"lsr.l      #3,%%d1                         \n\t"
		"and.l      %[mask],%%d1                    \n\t"
		"or.l       24(%[table],%%d1.w), %[pdata]   \n\t"
		"swap       %%d1                            \n\t"
		"or.l       16(%[table],%%d1.w), %[pdata]   \n\t"
		"movep.l    %[pdata], 0(%[out])             \n\t"
		"movem.l    (%[in])+, %%d0-%%d1             \n\t"
		"move.l     %%d0,%%d2                       \n\t"
		"lsl.l      #5,%%d2                         \n\t"
		"and.l      %[mask],%%d2                    \n\t"
		"move.l     12(%[table],%%d2.w), %[pdata]   \n\t"
		"swap       %%d2                            \n\t"
		"or.l       4(%[table],%%d2.w), %[pdata]    \n\t"
		"lsr.l      #3,%%d0                         \n\t"
		"and.l      %[mask],%%d0                    \n\t"
		"or.l       8(%[table],%%d0.w), %[pdata]    \n\t"
		"swap       %%d0                            \n\t"
		"or.l       (%[table],%%d0.w), %[pdata]     \n\t"
		"move.l     %%d1,%%d2                       \n\t"
		"lsl.l      #5,%%d2                         \n\t"
		"and.l      %[mask],%%d2                    \n\t"
		"or.l       28(%[table],%%d2.w), %[pdata]   \n\t"
		"swap       %%d2                            \n\t"
		"or.l       20(%[table],%%d2.w), %[pdata]   \n\t"
		"lsr.l      #3,%%d1                         \n\t"
		"and.l      %[mask],%%d1                    \n\t"
		"or.l       24(%[table],%%d1.w), %[pdata]   \n\t"
		"swap       %%d1                            \n\t"
		"or.l       16(%[table],%%d1.w), %[pdata]   \n\t"
		"movep.l    %[pdata], 1(%[out])             \n\t"
		"lea        8(%[out]), %[out]               \n\t"
		"dbra.w     %[groups],0b                    \n\t"
		: [out] "+a"(out), [in] "+a"(in), [pdata] "+d"(pdata), [groups] "+d"(groups)
		: [table] "a"(table), [mask] "d"(mask)
		: "d0", "d1", "d2", "memory");
}
#else
static void C2P_1x_Lorez_STDOOM(uint8_t *out, const uint8_t *in, unsigned short pixels, uint32_t (*table)[8])
{
	for (unsigned x = 0; x + 8 <= (unsigned)pixels; x += 8) {
		uint32_t pdata = 0;
		for (int k = 0; k < 8; k++)
			pdata |= table[in[x + (unsigned)k]][k];
		const unsigned group = x >> 4;
		const unsigned half = (x >> 3) & 1u;
		uint8_t *dst = out + group * 8u + half;
		C2P_Movep_Store(dst, pdata);
	}
}
#endif /* __m68k__ */

/* Map 8-bit logical color to ST index 0..15 using palette-opt weights + Bayer rank. */
static uint8_t C2P_STIndex_FromOptWeights(int x_mod4, int y_mod4, const uint8_t *w_row)
{
	const int b = (y_mod4 << 2) | x_mod4;
	const int rank = (int)Bayer4x4[b];
	int cum = 0;
	for (int k = 0; k < 16; k++) {
		cum += (int)w_row[k];
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
	const int yb = (abs_y & 3) << 2;
	return C2P_MapDither[yb | (abs_x & 3)][pal_idx];
}

/*
** Bake MapDither / nearest / clean4 / STDOOM_FragLUT from a 256×16 weight matrix read only
** during this rebuild (no copy of the matrix is retained in C2P).
**
** C2P_InitPairLUT_Once: fills C2P_PairLUT from nibble pairs (independent of weights; lazy one-time).
** MapDither[][]: Bayer cell (x&3,y&3) × weights -> ST nibble for C2P_Map8ToPlanar4.
** MapNearestLUT + PaletteIndexClean4LUT: one pass over each 16-entry weight row.
** STDOOM_FragLUT: movep-ready longword fragments for C2P_1x_Lorez_STDOOM (phase × pal × slot).
*/
static void C2P_Rebuild_Tables_From_WeightRows(const uint8_t (*weights)[16])
{
	C2P_InitPairLUT_Once();

	/* Per (x%4,y%4) Bayer cell: 8-bit VGA index -> dithered ST nibble (C2P_Map8ToPlanar4). */
	for (int b = 0; b < 16; b++) {
		const int xb = b & 3;
		const int yb = b >> 2;
		for (int src = 0; src < 256; src++) {
			C2P_MapDither[b][src] = C2P_STIndex_FromOptWeights(xb, yb, weights[src]);
		}
	}

	/*
	 * One pass per palette index: dominant ST color (C2P_MapNearestLUT) and "clean row"
	 * flag+color for C2P_Is_Palette_Index_Clean4 (C2P_PaletteIndexClean4LUT: bit7 + nibble).
	 */
	for (int src = 0; src < 256; src++) {
		const uint8_t *w = weights[src];
		int best_k = 0;
		int best_w = (int)w[0];
		int clean_k = -1;
		int nonzero = 0;
		for (int k = 0; k < 16; k++) {
			const int wk = (int)w[k];
			if (wk > best_w) {
				best_w = wk;
				best_k = k;
			}
			if (wk != 0) {
				nonzero++;
				clean_k = k;
			}
		}
		C2P_MapNearestLUT[src] = (uint8_t)best_k;
		C2P_PaletteIndexClean4LUT[src] =
			(nonzero == 1) ? (uint8_t)(0x80u | (unsigned)clean_k) : (uint8_t)0;
	}

	/* Full-screen lorez inner loop: phase × pal × pixel-slot -> OR-able movep fragments. */
	C2P_Rebuild_STDOOM_FragLUT(weights);
}

/* Rebuild LUTs from the built-in matrix selected by C2P_WeightSet. */
static void C2P_Rebuild_Tables_From_SelectedWeights(void)
{
	const uint8_t (*builtin)[16] =
		(C2P_WeightSet == C2P_WEIGHTSET_HTITLE) ? kC2PPaletteOptWeightHTitle : kC2PPaletteOptWeight;
	C2P_Rebuild_Tables_From_WeightRows(builtin);
}

extern "C" int C2P_Get_WeightSet(void)
{
	return C2P_WeightSet;
}

extern "C" void C2P_Select_WeightSet(int weight_set)
{
	const int normalized = (weight_set == C2P_WEIGHTSET_HTITLE) ? C2P_WEIGHTSET_HTITLE : C2P_WEIGHTSET_TEMPERAT;
	C2P_WeightSet = normalized;
	C2P_Rebuild_Tables_From_SelectedWeights();
}

extern "C" int C2P_Install_CustomWeights(const uint8_t *weights_256x16)
{
	if (!weights_256x16)
		return 0;

	C2P_Rebuild_Tables_From_WeightRows((const uint8_t (*)[16])(const void *)weights_256x16);
	return 1;
}

extern "C" void C2P_Clear_CustomWeights(void)
{
	C2P_Rebuild_Tables_From_SelectedWeights();
}

static inline void Planar_Put_Pixel_RowBytes(
	uint8_t *base, int row_bytes, int width_px, int height_px, int x, int y, uint8_t color4)
{
	if (!base || x < 0 || y < 0 || x >= width_px || y >= height_px || row_bytes <= 0)
		return;
	uint8_t *p = base + y * row_bytes + (x >> 4) * 8 + ((x >> 3) & 1);
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

extern "C" void C2P_Render_Logical_To_Planar_Rect(
	const uint8_t *logical,
	int logical_w,
	int logical_h,
	int logical_stride,
	uint8_t *planar_base,
	int planar_row_bytes,
	int planar_width_pixels,
	int planar_height_pixels,
	int dst_x0,
	int dst_y0,
	int abs_x0,
	int abs_y0)
{
	if (!logical || !planar_base || logical_w <= 0 || logical_h <= 0
		|| logical_stride <= 0 || planar_row_bytes <= 0
		|| planar_width_pixels <= 0 || planar_height_pixels <= 0) {
		return;
	}
	ST_FRAME_BAR_C2P_BEGIN();
	for (int y = 0; y < logical_h; y++) {
		const uint8_t *src = logical + (size_t)y * (size_t)logical_stride;
		const int apy = abs_y0 + y;
		const int yb = (apy & 3) << 2;
		uint8_t *dst_line = planar_base + (size_t)(dst_y0 + y) * (size_t)planar_row_bytes;
		const int row_unaligned = ((dst_x0 & 7) != 0);

		int x = 0;
		/*
		 * The pair-LUT movep path writes one 8-pixel halfword chunk at a fixed byte slot.
		 * It is only valid when destination x is 8-pixel aligned. For unaligned dst_x0,
		 * fall back to per-pixel writes so spans crossing 8-pixel boundaries are correct.
		 */
		for (; !row_unaligned && x + 8 <= logical_w; x += 8) {
			const int apx = abs_x0 + x;
			const int lx = dst_x0 + x;
			const int group = lx >> 4;
			const int half = (lx >> 3) & 1;
			uint8_t *dst = dst_line + group * 8 + half;

			const uint8_t c0 = C2P_MapDither[yb | ((apx + 0) & 3)][src[x + 0]];
			const uint8_t c1 = C2P_MapDither[yb | ((apx + 1) & 3)][src[x + 1]];
			const uint8_t c2 = C2P_MapDither[yb | ((apx + 2) & 3)][src[x + 2]];
			const uint8_t c3 = C2P_MapDither[yb | ((apx + 3) & 3)][src[x + 3]];
			const uint8_t c4 = C2P_MapDither[yb | ((apx + 4) & 3)][src[x + 4]];
			const uint8_t c5 = C2P_MapDither[yb | ((apx + 5) & 3)][src[x + 5]];
			const uint8_t c6 = C2P_MapDither[yb | ((apx + 6) & 3)][src[x + 6]];
			const uint8_t c7 = C2P_MapDither[yb | ((apx + 7) & 3)][src[x + 7]];

			const uint32_t v =
				C2P_PairLUT[0][(uint8_t)((c0 << 4) | c1)] |
				C2P_PairLUT[1][(uint8_t)((c2 << 4) | c3)] |
				C2P_PairLUT[2][(uint8_t)((c4 << 4) | c5)] |
				C2P_PairLUT[3][(uint8_t)((c6 << 4) | c7)];

			C2P_Movep_Store(dst, v);
		}
		/* Tail: widths not divisible by 8 (not used for 24x24 terrain). */
		for (; x < logical_w; x++) {
			const int apx = abs_x0 + x;
			const int lx = dst_x0 + x;
			const int ly = dst_y0 + y;
			const uint8_t nib = C2P_MapDither[yb | (apx & 3)][src[x]];
			Planar_Put_Pixel_RowBytes(
				planar_base,
				planar_row_bytes,
				planar_width_pixels,
				planar_height_pixels,
				lx,
				ly,
				nib);
		}
	}
	ST_FRAME_BAR_C2P_END();
}

extern "C" void C2P_Render_Logical_To_ST_Screen(
	const uint8_t *logical,
	int logical_stride,
	uint8_t *st_screen,
	int start_line_y,
	int end_line_y,
	int line_y_step)
{
	if (!logical || !st_screen || logical_stride <= 0)
		return;
	if (line_y_step < 1)
		return;

	ST_FRAME_BAR_C2P_BEGIN();
	/* LoRes mode */
	const int screen_width = 320;
	const int screen_height = C2P_ST_SCREEN_HEIGHT;
	const int bytes_per_line = 160; /* 20 groups * 8 bytes */

	if (start_line_y < 0 || start_line_y >= screen_height)
		return;
	if (end_line_y > screen_height)
		end_line_y = screen_height;
	if (end_line_y <= start_line_y)
		return;

	for (int y = start_line_y; y < end_line_y; y += line_y_step) {
		const uint8_t *src = logical + (size_t)y * (size_t)logical_stride;
		uint8_t *dst_line = st_screen + y * bytes_per_line;

		C2P_1x_Lorez_STDOOM(dst_line, src, (unsigned short)screen_width, C2P_STDOOM_FragLUT[y & 3]);
	}
	ST_FRAME_BAR_C2P_END();
}

extern "C" void ST_Planar_PutPixel(uint8_t *base, int row_bytes, int pw, int ph, int x, int y, unsigned char color4)
{
	Planar_Put_Pixel_RowBytes(base, row_bytes, pw, ph, x, y, color4);
}

static inline unsigned char Planar_Get_Pixel_RowBytes(
	const uint8_t *base, int row_bytes, int width_px, int height_px, int x, int y)
{
	if (!base || x < 0 || y < 0 || x >= width_px || y >= height_px || row_bytes <= 0)
		return 0;
	const uint8_t *p = base + y * row_bytes + (x >> 4) * 8 + ((x >> 3) & 1);
	const int bitnum = 7 - (x & 7);
	const uint8_t mask = (uint8_t)(1u << bitnum);
	uint8_t c = 0;
	for (int pl = 0; pl < 4; pl++) {
		if (p[pl * 2] & mask)
			c |= (uint8_t)(1u << pl);
	}
	return c;
}

extern "C" unsigned char ST_Planar_GetPixel(const uint8_t *base, int row_bytes, int pw, int ph, int x, int y)
{
	return Planar_Get_Pixel_RowBytes(base, row_bytes, pw, ph, x, y);
}

extern "C" void ST_Planar_Clear(uint8_t *base, int row_bytes, int pw, int ph, unsigned char color4)
{
	if (!base || row_bytes <= 0 || pw <= 0 || ph <= 0)
		return;
	const uint8_t c = (uint8_t)(color4 & 15);
	/*
	 * Avoid libc memset for ST screen clears here.
	 * On-target diagnostics show font data adjacent to the planar buffer being clobbered
	 * after Clear(0); using the explicit planar store loop keeps writes confined to the
	 * exact ST interleaved layout.
	 */
	C2P_InitPairLUT_Once();
	const uint8_t pair_idx = (uint8_t)((c << 4) | c);
	const uint32_t v =
		C2P_PairLUT[0][pair_idx] |
		C2P_PairLUT[1][pair_idx] |
		C2P_PairLUT[2][pair_idx] |
		C2P_PairLUT[3][pair_idx];
	for (int y = 0; y < ph; y++) {
		uint8_t *dst_line = base + y * row_bytes;
		for (int x = 0; x < pw; x += 8) {
			const int group = x >> 4;
			const int half = (x >> 3) & 1;
			uint8_t *dst = dst_line + group * 8 + half;
			C2P_Movep_Store(dst, v);
		}
	}
}

extern "C" void C2P_Fill_Aligned8_Rect(
	uint8_t *planar_base,
	int planar_row_bytes,
	int planar_width_pixels,
	int planar_height_pixels,
	int dst_x,
	int dst_y,
	int pixel_width,
	int pixel_height,
	unsigned char pal_idx)
{
	if (!planar_base || pixel_width <= 0 || pixel_height <= 0)
		return;
	if (planar_row_bytes <= 0 || planar_width_pixels <= 0 || planar_height_pixels <= 0)
		return;
	if ((dst_x & 7) != 0 || (pixel_width & 7) != 0)
		return;
	if (dst_x < 0 || dst_y < 0
		|| dst_x + pixel_width > planar_width_pixels
		|| dst_y + pixel_height > planar_height_pixels) {
		return;
	}

	ST_FRAME_BAR_C2P_BEGIN();
	C2P_InitPairLUT_Once();
	for (int y = 0; y < pixel_height; y++) {
		const int ay = dst_y + y;
		const int yb = (ay & 3) << 2;
		uint8_t *dst_line = planar_base + (size_t)ay * (size_t)planar_row_bytes;

		/*
		 * dst_x is 8-aligned, so the x dither phase starts at 0 and repeats for
		 * every 8-pixel group on the row.
		 */
		const uint8_t c0 = C2P_MapDither[yb | 0][pal_idx];
		const uint8_t c1 = C2P_MapDither[yb | 1][pal_idx];
		const uint8_t c2 = C2P_MapDither[yb | 2][pal_idx];
		const uint8_t c3 = C2P_MapDither[yb | 3][pal_idx];
		const uint32_t v =
			C2P_PairLUT[0][(uint8_t)((c0 << 4) | c1)] |
			C2P_PairLUT[1][(uint8_t)((c2 << 4) | c3)] |
			C2P_PairLUT[2][(uint8_t)((c0 << 4) | c1)] |
			C2P_PairLUT[3][(uint8_t)((c2 << 4) | c3)];

		for (int x = 0; x < pixel_width; x += 8) {
			const int ax = dst_x + x;
			uint8_t *dst = dst_line + (ax >> 4) * 8 + ((ax >> 3) & 1);
			C2P_Movep_Store(dst, v);
		}
	}
	ST_FRAME_BAR_C2P_END();
}

extern "C" void C2P_Blit_Linear8_To_Planar(
	uint8_t *planar_base,
	int dst_x, int dst_y,
	const uint8_t *src, int w, int h, int src_stride,
	int trans,
	int planar_row_bytes,
	int planar_width_pixels,
	int planar_height_pixels)
{
	if (!planar_base || !src || w <= 0 || h <= 0 || src_stride <= 0)
		return;
	if (planar_row_bytes <= 0 || planar_width_pixels <= 0 || planar_height_pixels <= 0)
		return;
	ST_FRAME_BAR_C2P_BEGIN();
	C2P_InitPairLUT_Once();

	const int row_bytes = planar_row_bytes;
	const int pw = planar_width_pixels;
	const int ph = planar_height_pixels;

	for (int yy = 0; yy < h; yy++) {
		const int py = dst_y + yy;
		if (py < 0 || py >= ph)
			continue;

		const int yb = (py & 3) << 2;
		const uint8_t *srcrow = src + (size_t)yy * (size_t)src_stride;

		int xx_lo = 0;
		int xx_hi = w;
		if (dst_x < 0) {
			xx_lo = -dst_x;
		}
		if (dst_x + w > pw) {
			xx_hi = w - ((dst_x + w) - pw);
		}
		if (xx_lo >= xx_hi)
			continue;

		const int logical_w = xx_hi - xx_lo;
		const int dst_x0 = dst_x + xx_lo;
		const int abs_x0 = dst_x + xx_lo;
		const uint8_t *s = srcrow + xx_lo;
		uint8_t *dst_line = planar_base + (size_t)py * (size_t)row_bytes;

		int x = 0;

		if (!trans) {
			for (; x < logical_w && ((dst_x0 + x) & 7) != 0; x++) {
				const int px = dst_x0 + x;
				const uint8_t nib = C2P_MapDither[yb | (px & 3)][s[x]];
				Planar_Put_Pixel_RowBytes(planar_base, row_bytes, pw, ph, px, py, nib);
			}
			for (; x + 8 <= logical_w; x += 8) {
				const int apx = abs_x0 + x;
				const int lx = dst_x0 + x;
				uint8_t *dst = dst_line + (lx >> 4) * 8 + ((lx >> 3) & 1);

				const uint8_t c0 = C2P_MapDither[yb | ((apx + 0) & 3)][s[x + 0]];
				const uint8_t c1 = C2P_MapDither[yb | ((apx + 1) & 3)][s[x + 1]];
				const uint8_t c2 = C2P_MapDither[yb | ((apx + 2) & 3)][s[x + 2]];
				const uint8_t c3 = C2P_MapDither[yb | ((apx + 3) & 3)][s[x + 3]];
				const uint8_t c4 = C2P_MapDither[yb | ((apx + 4) & 3)][s[x + 4]];
				const uint8_t c5 = C2P_MapDither[yb | ((apx + 5) & 3)][s[x + 5]];
				const uint8_t c6 = C2P_MapDither[yb | ((apx + 6) & 3)][s[x + 6]];
				const uint8_t c7 = C2P_MapDither[yb | ((apx + 7) & 3)][s[x + 7]];

				const uint32_t vv =
				    C2P_PairLUT[0][(uint8_t)((c0 << 4) | c1)] |
				    C2P_PairLUT[1][(uint8_t)((c2 << 4) | c3)] |
				    C2P_PairLUT[2][(uint8_t)((c4 << 4) | c5)] |
				    C2P_PairLUT[3][(uint8_t)((c6 << 4) | c7)];

				C2P_Movep_Store(dst, vv);
			}
			for (; x < logical_w; x++) {
				const int px = dst_x0 + x;
				const uint8_t nib = C2P_MapDither[yb | ((abs_x0 + x) & 3)][s[x]];
				Planar_Put_Pixel_RowBytes(planar_base, row_bytes, pw, ph, px, py, nib);
			}
		} else {
			for (; x < logical_w && ((dst_x0 + x) & 7) != 0; x++) {
				const uint8_t sp = s[x];
				if (sp == 0)
					continue;
				const int px = dst_x0 + x;
				const uint8_t nib = C2P_MapDither[yb | (px & 3)][sp];
				Planar_Put_Pixel_RowBytes(planar_base, row_bytes, pw, ph, px, py, nib);
			}
			for (; x + 8 <= logical_w; x += 8) {
				int nonzero = 0;
				for (int k = 0; k < 8; k++) {
					if (s[x + k] != 0) {
						nonzero = 1;
						break;
					}
				}
				if (nonzero == 0)
					continue;

				const int apx = abs_x0 + x;
				const int lx = dst_x0 + x;
				int opaque8 = 1;
				for (int k = 0; k < 8; k++) {
					if (s[x + k] == 0) {
						opaque8 = 0;
						break;
					}
				}

				if (opaque8) {
					uint8_t *dst = dst_line + (lx >> 4) * 8 + ((lx >> 3) & 1);
					const uint8_t c0 = C2P_MapDither[yb | ((apx + 0) & 3)][s[x + 0]];
					const uint8_t c1 = C2P_MapDither[yb | ((apx + 1) & 3)][s[x + 1]];
					const uint8_t c2 = C2P_MapDither[yb | ((apx + 2) & 3)][s[x + 2]];
					const uint8_t c3 = C2P_MapDither[yb | ((apx + 3) & 3)][s[x + 3]];
					const uint8_t c4 = C2P_MapDither[yb | ((apx + 4) & 3)][s[x + 4]];
					const uint8_t c5 = C2P_MapDither[yb | ((apx + 5) & 3)][s[x + 5]];
					const uint8_t c6 = C2P_MapDither[yb | ((apx + 6) & 3)][s[x + 6]];
					const uint8_t c7 = C2P_MapDither[yb | ((apx + 7) & 3)][s[x + 7]];

					const uint32_t vv =
					    C2P_PairLUT[0][(uint8_t)((c0 << 4) | c1)] |
					    C2P_PairLUT[1][(uint8_t)((c2 << 4) | c3)] |
					    C2P_PairLUT[2][(uint8_t)((c4 << 4) | c5)] |
					    C2P_PairLUT[3][(uint8_t)((c6 << 4) | c7)];

					C2P_Movep_Store(dst, vv);
				} else {
					for (int k = 0; k < 8; k++) {
						const uint8_t sp = s[x + k];
						if (sp == 0)
							continue;
						const int px = dst_x0 + x + k;
						const uint8_t nib = C2P_MapDither[yb | (px & 3)][sp];
						Planar_Put_Pixel_RowBytes(planar_base, row_bytes, pw, ph, px, py, nib);
					}
				}
			}
			for (; x < logical_w; x++) {
				const uint8_t sp = s[x];
				if (sp == 0)
					continue;
				const int px = dst_x0 + x;
				const uint8_t nib = C2P_MapDither[yb | (px & 3)][sp];
				Planar_Put_Pixel_RowBytes(planar_base, row_bytes, pw, ph, px, py, nib);
			}
		}
	}
	ST_FRAME_BAR_C2P_END();
}

