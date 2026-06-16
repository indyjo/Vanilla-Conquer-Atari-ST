/*
 * ST hardware blitter: planar rectangle copy with skew/masks.
 * Forward skew/endmask/X_Count follow Atari BLIT_iT (1987 Blitter manual, Appendix A).
 */

#include "st_blitter_blit.h"

#include "st_cache.h"
#include "st_frame_meter.h"
#include <stddef.h>

static void ST_Blit_Cache_Rect_Span(
	const uint8_t *base, int row_bytes, int y_abs, int pixel_height,
	const void **out_start, size_t *out_len)
{
	const uint8_t *row0 = base + (size_t)y_abs * (size_t)row_bytes;
	*out_start = row0;
	*out_len = (size_t)row_bytes * (size_t)pixel_height;
}

static void ST_Blit_Sync_Cache_Before(
	const uint8_t *src_root, int src_row_bytes, int sy_abs, int pixel_height,
	uint8_t *dst_root, int dst_row_bytes, int dy_abs)
{
	const void *src_start = NULL;
	const void *dst_start = NULL;
	size_t src_len = 0;
	size_t dst_len = 0;
	ST_Blit_Cache_Rect_Span(src_root, src_row_bytes, sy_abs, pixel_height, &src_start, &src_len);
	ST_Blit_Cache_Rect_Span(dst_root, dst_row_bytes, dy_abs, pixel_height, &dst_start, &dst_len);
	ST_Cache_Push_Range(src_start, src_len);
	ST_Cache_Push_Range(dst_start, dst_len);
}

static void ST_Blit_Sync_Cache_After(
	const uint8_t *src_root, int src_row_bytes, int sy_abs, int pixel_height,
	uint8_t *dst_root, int dst_row_bytes, int dy_abs,
	BOOL same_surface)
{
	const void *dst_start = NULL;
	size_t dst_len = 0;
	ST_Blit_Cache_Rect_Span(dst_root, dst_row_bytes, dy_abs, pixel_height, &dst_start, &dst_len);
	ST_Cache_Invalidate_Range(dst_start, dst_len);
	if (same_surface) {
		const void *src_start = NULL;
		size_t src_len = 0;
		ST_Blit_Cache_Rect_Span(src_root, src_row_bytes, sy_abs, pixel_height, &src_start, &src_len);
		ST_Cache_Invalidate_Range(src_start, src_len);
	}
}

static inline volatile unsigned short *ST_BLT_REG(unsigned long addr)
{
	return (volatile unsigned short *)addr;
}

static void ST_Blit_Wait_Idle(void)
{
	volatile unsigned char *ctrl = (volatile unsigned char *)0xFFFF8A3CUL;
	while ((*ctrl & 0x80u) != 0) {
	}
}

static BOOL ST_Has_Blitter(void)
{
	return TRUE;
}

/* BLIT_iT skew_flags[] — left-to-right, indices 0..7 (Appendix A). */
enum {
	ST_BLIT_SKEW_NFSR = 0x40u, /* skew reg bit 6 */
	ST_BLIT_SKEW_FXSR = 0x80u  /* skew reg bit 7 */
};

static const unsigned char k_skew_fxsr_nfsr[8] = {
	/* Multi-word destination span */
	ST_BLIT_SKEW_NFSR,                     /* 0: src span < dst span */
	ST_BLIT_SKEW_FXSR,                     /* 1: src span > dst span */
	0x00u,                                 /* 2: equal spans, sm <= dm */
	ST_BLIT_SKEW_NFSR + ST_BLIT_SKEW_FXSR, /* 3: equal spans, sm > dm */
	/* Single-word destination span */
	0x00u,                                 /* 4: src span 0 */
	ST_BLIT_SKEW_FXSR,                     /* 5: src span 2 words */
	0x00u,                                 /* 6: src & dst both 1 word, skew != 0 */
	0x00u                                  /* 7: src & dst both 1 word */
};

enum {
	ST_BLITTER_SHORT_MAX = 32767
};

typedef struct {
	short dst_words;
	short src_words;
	unsigned char skew_reg;
	unsigned short endmask1;
	unsigned short endmask2;
	unsigned short endmask3;
	BOOL reverse_x;
	BOOL reverse_y;
} ST_Blit_BLIT_iT_Plan;

/*
 * BLIT_iT endmask/skew/X_Count setup (Appendix A). reverse_x/reverse_y are set
 * by the caller for same-surface overlap scroll (manual §Overlap).
 */
static BOOL ST_Blit_Compute_BLIT_iT(
	int sx_abs,
	int dx_abs,
	int pixel_width,
	BOOL reverse_x,
	BOOL reverse_y,
	ST_Blit_BLIT_iT_Plan *plan)
{
	if (!plan || pixel_width <= 0) {
		return FALSE;
	}

	/* Compute the number of source and destination words that will be blitted. */
	const short dst_words = (short)(((dx_abs + pixel_width - 1) >> 4) - (dx_abs >> 4) + 1);
	const short src_words = (short)(((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4) + 1);
	if (dst_words <= 0 || src_words <= 0) {
		return FALSE;
	}

	// Source and destination pixel modulo 16.
	const unsigned char sm = (unsigned char)(sx_abs & 15u);
	const unsigned char dm = (unsigned char)(dx_abs & 15u);
	// How many pixels to skew the source to the destination.
	const unsigned char skew_low =
		(unsigned char)(((unsigned)dm + 16u - (unsigned)sm) % 16u);

	// Compose the skew index out of three components:
	// 1. Is the skew going to the left (which means fxsr is needed).
	short skew_idx = (sm > dm) ? 1 : 0;
	// 2. Do source and destination have the same number of words.
	if (src_words == dst_words) {
		skew_idx += 2;
	}
	// 3. Is the destination a single word (which means endmasks collapse).
	if (dst_words == 1) {
		skew_idx += 4;
	}

	plan->reverse_x = reverse_x;
	plan->reverse_y = reverse_y;
	if (reverse_x && sm != dm) {
		skew_idx ^= 1;
	}
	// Clear all other bits.
	skew_idx &= 7;

	/* lf_endmask / rt_endmask equivalent (BLIT_iT tables). */
	unsigned short endmask1 = (unsigned short)(0xFFFFu >> dm);
	unsigned short endmask3 = (unsigned short)(0xFFFFu << (15 - ((dx_abs + pixel_width - 1) & 15)));
	if (dst_words == 1) {
		// Collapse endmasks if the destination is a single word.
		endmask1 = (unsigned short)(endmask1 & endmask3);
		endmask3 = endmask1;
	} else if (plan->reverse_x) {
		// Swap endmasks if we are reversing the x direction.
		const unsigned short t = endmask1;
		endmask1 = endmask3;
		endmask3 = t;
	}

	const unsigned char skew_reg =
		(unsigned char)(skew_low | k_skew_fxsr_nfsr[skew_idx]);
	/* One word per line + skew: FXSR primes the shift latch; Src_Xinc must be 0
	 * (set in Run_Plane) so the prefetch re-reads the same word, not word+1. */
	const unsigned char skew_out =
		(src_words == 1 && dst_words == 1 && skew_low != 0)
		? (unsigned char)(skew_low | ST_BLIT_SKEW_FXSR)
		: skew_reg;

	plan->dst_words = dst_words;
	plan->src_words = src_words;
	plan->skew_reg = skew_out;
	plan->endmask1 = endmask1;
	plan->endmask2 = 0xFFFFu;
	plan->endmask3 = endmask3;
	if (dst_words == 1) {
		plan->endmask2 = endmask1;
	}
	return TRUE;
}

/*
 * Program one plane per BLIT_iT register formulas:
 *   Src_Yinc = Src_NXLN - Src_NXWD * (src_words - 1)
 *   Dst_Yinc = Dst_NXLN - Dst_NXWD * (dst_words - 1)
 *   X_Count  = dst_words
 * No FXSR/NFSR tweaks to Src_Yinc — BLIT_iT span already matches actual reads.
 */
static void ST_Blit_Run_Plane(
	const uint8_t *src_base, uint8_t *dst_base,
	short src_row_bytes, short dst_row_bytes,
	short src_x_inc, short dst_x_inc,
	short src_word_bytes, short dst_word_bytes,
	const ST_Blit_BLIT_iT_Plan *plan,
	short lines, short plane_idx,
	unsigned char blit_op)
{
	const short dst_words = plan->dst_words;
	const short src_words = plan->src_words;
	if (!src_base || !dst_base || dst_words <= 0 || src_words <= 0 || lines <= 0
		|| plane_idx < 0 || plane_idx > 3) {
		return;
	}

	if (plan->reverse_x) {
		src_x_inc = (short)-src_x_inc;
		dst_x_inc = (short)-dst_x_inc;
	}

	/* X_Count==1: hardware ignores X inc for the transfer, but FXSR still steps it. */
	if (dst_words == 1 && src_words == 1 && (plan->skew_reg & 0x0Fu) != 0u) {
		src_x_inc = 0;
		dst_x_inc = 0;
	}

	const short src_row = plan->reverse_y ? (short)-src_row_bytes : src_row_bytes;
	const short dst_row = plan->reverse_y ? (short)-dst_row_bytes : dst_row_bytes;

	const short dst_y_inc = (short)(dst_row - (dst_words - 1) * dst_x_inc);
	const short src_y_inc = (short)(src_row - (src_words - 1) * src_x_inc);

	const uint8_t *src_start = src_base
		+ (plan->reverse_y ? (size_t)(lines - 1) * (size_t)src_row_bytes : 0u)
		+ (plan->reverse_x ? (size_t)(src_words - 1) * (size_t)src_word_bytes : 0u);
	uint8_t *dst_start = dst_base
		+ (plan->reverse_y ? (size_t)(lines - 1) * (size_t)dst_row_bytes : 0u)
		+ (plan->reverse_x ? (size_t)(dst_words - 1) * (size_t)dst_word_bytes : 0u);

	ST_Blit_Wait_Idle();
	*ST_BLT_REG(0xFFFF8A20UL) = (unsigned short)src_x_inc;
	*ST_BLT_REG(0xFFFF8A22UL) = (unsigned short)src_y_inc;
	{
		size_t sa = (size_t)src_start;
		*ST_BLT_REG(0xFFFF8A24UL) = (unsigned short)(sa >> 16);
		*ST_BLT_REG(0xFFFF8A26UL) = (unsigned short)(sa & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A28UL) = plan->endmask1;
	*ST_BLT_REG(0xFFFF8A2AUL) = plan->endmask2;
	*ST_BLT_REG(0xFFFF8A2CUL) = plan->endmask3;
	*ST_BLT_REG(0xFFFF8A2EUL) = (unsigned short)dst_x_inc;
	*ST_BLT_REG(0xFFFF8A30UL) = (unsigned short)dst_y_inc;
	{
		size_t da = (size_t)(dst_start + plane_idx * 2);
		*ST_BLT_REG(0xFFFF8A32UL) = (unsigned short)(da >> 16);
		*ST_BLT_REG(0xFFFF8A34UL) = (unsigned short)(da & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A36UL) = (unsigned short)dst_words;
	*ST_BLT_REG(0xFFFF8A38UL) = (unsigned short)lines;
	*(volatile unsigned char *)0xFFFF8A3AUL = 2;
	*(volatile unsigned char *)0xFFFF8A3BUL = blit_op;
	*(volatile unsigned char *)0xFFFF8A3DUL = plan->skew_reg;
	*(volatile unsigned char *)0xFFFF8A3CUL = 0x80;
	ST_Blit_Wait_Idle();
}

BOOL ST_Blitter_Planar_Screen_Rect_Blit(
	const uint8_t *src_root,
	uint8_t *dst_root,
	int sx_abs,
	int sy_abs,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Planar_Rect_Blit(
		src_root,
		ST_PLANAR_BYTES_PER_LINE,
		sx_abs,
		sy_abs,
		dst_root,
		ST_PLANAR_BYTES_PER_LINE,
		dx_abs,
		dy_abs,
		pixel_width,
		pixel_height);
}

static BOOL ST_Blitter_Planar_Rect_Blit_With_Op(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height,
	unsigned char blit_op)
{
	if (!src_root || !dst_root || !ST_Has_Blitter())
		return FALSE;

	if (pixel_width <= 0 || pixel_height <= 0
		|| src_row_bytes <= 0 || dst_row_bytes <= 0
		|| src_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX) {
		return FALSE;
	}

	const BOOL same_surface = (src_root == dst_root);
	const BOOL reverse_x = same_surface && (dx_abs > sx_abs);
	const BOOL reverse_y = same_surface && (dy_abs > sy_abs);
	ST_Blit_BLIT_iT_Plan plan;
	if (!ST_Blit_Compute_BLIT_iT(
			sx_abs,
			dx_abs,
			pixel_width,
			reverse_x,
			reverse_y,
			&plan)) {
		return FALSE;
	}

	// Compute address of top-left source and destination words.
	const uint8_t *src = src_root + (size_t)sy_abs * (size_t)src_row_bytes
		+ (size_t)(sx_abs >> 4) * 8;
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)(dx_abs >> 4) * 8;

	ST_Blit_Sync_Cache_Before(
		src_root, src_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs);
	ST_FRAME_BAR_BLIT_BEGIN();
	for (short pl = 0; pl < 4; ++pl) {
		ST_Blit_Run_Plane(
			src + (size_t)pl * 2u,
			dst,
			(short)src_row_bytes,
			(short)dst_row_bytes,
			8,
			8,
			8,
			8,
			&plan,
			(short)pixel_height,
			pl,
			blit_op);
	}
	ST_FRAME_BAR_BLIT_END();
	ST_Blit_Sync_Cache_After(
		src_root, src_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs,
		same_surface);
	return TRUE;
}

BOOL ST_Blitter_Planar_Rect_Blit(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Planar_Rect_Blit_With_Op(
		src_root, src_row_bytes,
		sx_abs, sy_abs, dst_root, dst_row_bytes,
		dx_abs, dy_abs, pixel_width, pixel_height,
		3);
}

BOOL ST_Blitter_Planar_Rect_Blit_Or(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Planar_Rect_Blit_With_Op(
		src_root, src_row_bytes,
		sx_abs, sy_abs, dst_root, dst_row_bytes,
		dx_abs, dy_abs, pixel_width, pixel_height,
		7);
}

BOOL ST_Blitter_Mask_And_Planar_Rect(
	const uint8_t *mask_root,
	int mask_row_bytes,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	if (!mask_root || !dst_root || !ST_Has_Blitter())
		return FALSE;

	if (pixel_width <= 0 || pixel_height <= 0
		|| mask_row_bytes <= 0 || dst_row_bytes <= 0
		|| mask_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX) {
		return FALSE;
	}

	ST_Blit_BLIT_iT_Plan plan;
	if (!ST_Blit_Compute_BLIT_iT(
			sx_abs,
			dx_abs,
			pixel_width,
			FALSE,
			FALSE,
			&plan)) {
		return FALSE;
	}

	const short src_word_left = (short)(sx_abs & ~15);
	const short dst_word_left = (short)(dx_abs & ~15);
	const uint8_t *src = mask_root + (size_t)sy_abs * (size_t)mask_row_bytes
		+ (size_t)((src_word_left >> 4) * 2);
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)((dst_word_left >> 4) * 8);

	ST_Blit_Sync_Cache_Before(
		mask_root, mask_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs);
	ST_FRAME_BAR_BLIT_BEGIN();
	for (short pl = 0; pl < 4; ++pl) {
		ST_Blit_Run_Plane(
			src,
			dst,
			(short)mask_row_bytes,
			(short)dst_row_bytes,
			2,
			8,
			2,
			8,
			&plan,
			(short)pixel_height,
			pl,
			1);
	}
	ST_FRAME_BAR_BLIT_END();
	ST_Blit_Sync_Cache_After(
		mask_root, mask_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs,
		FALSE);
	return TRUE;
}
