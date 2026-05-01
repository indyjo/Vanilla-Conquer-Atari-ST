/*
 * ST hardware blitter: planar 320x200 framebuffer rectangle copy with skew/masks.
 */

#include "st_blitter_blit.h"

#include <stddef.h>

/*
 * Return a word-sized view of a BLiTTER register. Register addresses are
 * constants below, so this keeps the programming sequence readable.
 */
static inline volatile unsigned short *ST_BLT_REG(unsigned long addr)
{
	return (volatile unsigned short *)addr;
}

/*
 * Wait until the BLiTTER is idle before reprogramming registers or returning
 * to code that may touch the same memory.
 */
static void ST_Blit_Wait_Idle(void)
{
	volatile unsigned char *ctrl = (volatile unsigned char *)0xFFFF8A3CUL;
	while ((*ctrl & 0x80u) != 0) {
	}
}

/*
 * The Atari ST startup path requires and enables the hardware BLiTTER, so this
 * is a policy hook rather than runtime probing.
 */
static BOOL ST_Has_Blitter(void)
{
	/* startup.cpp already requires and enables ST hardware blitter. */
	return TRUE;
}

/*
 * Atari BLiTTER skew register (0xFFFF8A3D): low nibble = skew count; bits 6–7 NFSR/FXSR
 * per official sample (BLiTTER_1-25-1990). Index matches the assembler's d6 skew table.
 */
static const unsigned char k_skew_fxsr_nfsr[8] = {
	0x40u, 0x80u, 0x00u, 0xc0u,
	0x00u, 0x80u, 0x00u, 0x00u
};

enum {
	ST_BLITTER_SHORT_MAX = 32767
};

/*
 * Copy one interleaved ST bitplane from a planar source rectangle to a planar
 * destination rectangle. The caller has already expanded the pixel rectangle to
 * BLiTTER words, selected masks/skew, and handled same-surface direction.
 */
static void ST_Blit_Copy_Plane_Skew_Masked(
	const uint8_t *src_base, uint8_t *dst_base,
	short src_row_bytes, short dst_row_bytes,
	short words, short lines, short plane_idx,
	unsigned char skew_reg,
	BOOL reverse_x,
	BOOL reverse_y,
	unsigned short endmask1,
	unsigned short endmask2,
	unsigned short endmask3,
	unsigned char blit_op)
{
	if (!src_base || !dst_base || words <= 0 || lines <= 0 || plane_idx < 0 || plane_idx > 3) {
		return;
	}
	const short sx = reverse_x ? -8 : 8;
	const short dx = reverse_x ? -8 : 8;
	const short src_row_advance = reverse_y ? (short)-src_row_bytes : src_row_bytes;
	const short dst_row_advance = reverse_y ? (short)-dst_row_bytes : dst_row_bytes;
	short src_y_inc = (short)(src_row_advance - (words - 1) * sx);
	const short dst_y_inc = (short)(dst_row_advance - (words - 1) * dx);
	if (skew_reg & 0x80u) {
		src_y_inc = (short)(src_y_inc - sx); /* FXSR: one extra source word read at line start */
	}
	if (skew_reg & 0x40u) {
		src_y_inc = (short)(src_y_inc + sx); /* NFSR: suppress final source read on the line */
	}
	const uint8_t *src_start = src_base
		+ (reverse_y ? (size_t)(lines - 1) * (size_t)src_row_bytes : 0u)
		+ (reverse_x ? (size_t)(words - 1) * 8u : 0u);
	uint8_t *dst_start = dst_base
		+ (reverse_y ? (size_t)(lines - 1) * (size_t)dst_row_bytes : 0u)
		+ (reverse_x ? (size_t)(words - 1) * 8u : 0u);

	ST_Blit_Wait_Idle();
	*ST_BLT_REG(0xFFFF8A20UL) = (unsigned short)sx;
	*ST_BLT_REG(0xFFFF8A22UL) = (unsigned short)src_y_inc;
	{
		size_t sa = (size_t)(src_start + plane_idx * 2);
		*ST_BLT_REG(0xFFFF8A24UL) = (unsigned short)(sa >> 16);
		*ST_BLT_REG(0xFFFF8A26UL) = (unsigned short)(sa & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A28UL) = endmask1;
	*ST_BLT_REG(0xFFFF8A2AUL) = endmask2;
	*ST_BLT_REG(0xFFFF8A2CUL) = endmask3;
	*ST_BLT_REG(0xFFFF8A2EUL) = (unsigned short)dx;
	*ST_BLT_REG(0xFFFF8A30UL) = (unsigned short)dst_y_inc;
	{
		size_t da = (size_t)(dst_start + plane_idx * 2);
		*ST_BLT_REG(0xFFFF8A32UL) = (unsigned short)(da >> 16);
		*ST_BLT_REG(0xFFFF8A34UL) = (unsigned short)(da & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A36UL) = (unsigned short)words;
	*ST_BLT_REG(0xFFFF8A38UL) = (unsigned short)lines;
	*(volatile unsigned char *)0xFFFF8A3AUL = 2; /* HOP: source */
	*(volatile unsigned char *)0xFFFF8A3BUL = blit_op; /* 3=D=S, 7=D|merge */
	*(volatile unsigned char *)0xFFFF8A3DUL = skew_reg;
	*(volatile unsigned char *)0xFFFF8A3CUL = 0x80; /* start */
	ST_Blit_Wait_Idle();
}

/*
 * Apply one 1bpp mask word stream to a single destination bitplane. The source
 * advances by 2 bytes per BLiTTER word; the destination advances by 8 bytes
 * because ST low-res stores four interleaved planes per 16-pixel group.
 */
static void ST_Blit_And_Mask_To_Plane_Skew_Masked(
	const uint8_t *src_base, uint8_t *dst_base,
	short src_row_bytes, short dst_row_bytes,
	short words, short lines, short plane_idx,
	unsigned char skew_reg,
	BOOL reverse_x,
	BOOL reverse_y,
	unsigned short endmask1,
	unsigned short endmask2,
	unsigned short endmask3)
{
	if (!src_base || !dst_base || words <= 0 || lines <= 0 || plane_idx < 0 || plane_idx > 3) {
		return;
	}
	const short sx = reverse_x ? -2 : 2;
	const short dx = reverse_x ? -8 : 8;
	const short src_row_advance = reverse_y ? (short)-src_row_bytes : src_row_bytes;
	const short dst_row_advance = reverse_y ? (short)-dst_row_bytes : dst_row_bytes;
	short src_y_inc = (short)(src_row_advance - (words - 1) * sx);
	const short dst_y_inc = (short)(dst_row_advance - (words - 1) * dx);
	const uint8_t *src_start = src_base
		+ (reverse_y ? (size_t)(lines - 1) * (size_t)src_row_bytes : 0u)
		+ (reverse_x ? (size_t)(words - 1) * 2u : 0u);
	uint8_t *dst_start = dst_base
		+ (reverse_y ? (size_t)(lines - 1) * (size_t)dst_row_bytes : 0u)
		+ (reverse_x ? (size_t)(words - 1) * 8u : 0u);

	if (skew_reg & 0x80u) {
		src_y_inc = (short)(src_y_inc - sx); /* FXSR performs one extra source read. */
	}
	if (skew_reg & 0x40u) {
		src_y_inc = (short)(src_y_inc + sx); /* NFSR suppresses the final source read. */
	}

	ST_Blit_Wait_Idle();
	*ST_BLT_REG(0xFFFF8A20UL) = (unsigned short)sx;
	*ST_BLT_REG(0xFFFF8A22UL) = (unsigned short)src_y_inc;
	{
		size_t sa = (size_t)src_start;
		*ST_BLT_REG(0xFFFF8A24UL) = (unsigned short)(sa >> 16);
		*ST_BLT_REG(0xFFFF8A26UL) = (unsigned short)(sa & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A28UL) = endmask1;
	*ST_BLT_REG(0xFFFF8A2AUL) = endmask2;
	*ST_BLT_REG(0xFFFF8A2CUL) = endmask3;
	*ST_BLT_REG(0xFFFF8A2EUL) = (unsigned short)dx;
	*ST_BLT_REG(0xFFFF8A30UL) = (unsigned short)dst_y_inc;
	{
		size_t da = (size_t)(dst_start + plane_idx * 2);
		*ST_BLT_REG(0xFFFF8A32UL) = (unsigned short)(da >> 16);
		*ST_BLT_REG(0xFFFF8A34UL) = (unsigned short)(da & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A36UL) = (unsigned short)words;
	*ST_BLT_REG(0xFFFF8A38UL) = (unsigned short)lines;
	*(volatile unsigned char *)0xFFFF8A3AUL = 2; /* HOP: source */
	*(volatile unsigned char *)0xFFFF8A3BUL = 1; /* OP: D = S & D */
	*(volatile unsigned char *)0xFFFF8A3DUL = skew_reg;
	*(volatile unsigned char *)0xFFFF8A3CUL = 0x80; /* start */
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
		ST_PLANAR_WIDTH,
		ST_PLANAR_HEIGHT,
		sx_abs,
		sy_abs,
		dst_root,
		ST_PLANAR_BYTES_PER_LINE,
		ST_PLANAR_WIDTH,
		ST_PLANAR_HEIGHT,
		dx_abs,
		dy_abs,
		pixel_width,
		pixel_height);
}


static BOOL ST_Blitter_Planar_Rect_Blit_With_Op(
	const uint8_t *src_root,
	int src_row_bytes,
	int src_width_pixels,
	int src_height_pixels,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height,
	unsigned char blit_op)
{
	if (!src_root || !dst_root || !ST_Has_Blitter())
		return FALSE;

	if (pixel_width <= 0 || pixel_height <= 0
		|| sx_abs < 0 || sy_abs < 0 || dx_abs < 0 || dy_abs < 0
		|| src_row_bytes <= 0 || dst_row_bytes <= 0
		|| src_width_pixels <= 0 || src_height_pixels <= 0
		|| dst_width_pixels <= 0 || dst_height_pixels <= 0
		|| src_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| src_width_pixels > ST_BLITTER_SHORT_MAX
		|| dst_width_pixels > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX
		|| sx_abs + pixel_width > src_width_pixels
		|| dx_abs + pixel_width > dst_width_pixels
		|| sy_abs + pixel_height > src_height_pixels
		|| dy_abs + pixel_height > dst_height_pixels) {
		return FALSE;
	}

	const short dst_word_left = (short)(dx_abs & ~15);
	const short dst_start = (short)(dx_abs - dst_word_left);
	const short words = (short)((dst_start + pixel_width + 15) >> 4);
	const short src_word_left = (short)(sx_abs & ~15);
	/* Source RAM must cover the sprite 16-pixel word run from sx, not dest word count * 16. */
	const short src_words = (short)(((sx_abs & 15) + pixel_width + 15) >> 4);
	if (words <= 0 || src_word_left < 0 || (src_word_left + src_words * 16) > src_width_pixels) {
		return FALSE;
	}

	const unsigned char skew_low = (unsigned char)(((unsigned)(dx_abs & 15u) + 16u - (unsigned)(sx_abs & 15u)) % 16u);
	unsigned char sm = (unsigned char)(sx_abs & 15u);
	unsigned char dm = (unsigned char)(dx_abs & 15u);
	short skew_idx = (sm > dm) ? 1 : 0;
	const short src_span_m1 = (short)(((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4));
	const short dst_span_m1 = (short)(((dx_abs + pixel_width - 1) >> 4) - (dx_abs >> 4));
	if (dst_span_m1 == 0)
		skew_idx += 4;
	if (src_span_m1 == dst_span_m1)
		skew_idx += 2;

	const BOOL same_surface = (src_root == dst_root);
	const BOOL reverse_x = same_surface && (dx_abs > sx_abs);
	const BOOL reverse_y = same_surface && (dy_abs > sy_abs);

	if (reverse_x && sm != dm)
		skew_idx ^= 1;
	if (skew_idx > 7)
		skew_idx &= 7;

	const short start = (short)(dx_abs & 15);
	const short end = (short)((dx_abs + pixel_width - 1) & 15);
	unsigned short endmask1 = (unsigned short)(0xFFFFu >> start);
	unsigned short endmask3 = (unsigned short)(0xFFFFu << (15 - end));
	unsigned short endmask2 = 0xFFFFu;
	if (words == 1) {
		endmask1 = (unsigned short)(endmask1 & endmask3);
		endmask2 = endmask1;
		endmask3 = endmask1;
	} else if (reverse_x) {
		unsigned short t = endmask1;
		endmask1 = endmask3;
		endmask3 = t;
	}

	const uint8_t *src = src_root + (size_t)sy_abs * (size_t)src_row_bytes
		+ (size_t)((src_word_left >> 4) * 8);
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)((dst_word_left >> 4) * 8);
	const unsigned char skew_reg = (unsigned char)(skew_low | k_skew_fxsr_nfsr[skew_idx]);

	for (short pl = 0; pl < 4; ++pl) {
		ST_Blit_Copy_Plane_Skew_Masked(
			src,
			dst,
			(short)src_row_bytes,
			(short)dst_row_bytes,
			words,
			(short)pixel_height,
			pl,
			skew_reg,
			reverse_x,
			reverse_y,
			endmask1,
			endmask2,
			endmask3,
			blit_op);
	}
	return TRUE;
}

BOOL ST_Blitter_Planar_Rect_Blit(
	const uint8_t *src_root,
	int src_row_bytes,
	int src_width_pixels,
	int src_height_pixels,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Planar_Rect_Blit_With_Op(
		src_root, src_row_bytes, src_width_pixels, src_height_pixels,
		sx_abs, sy_abs, dst_root, dst_row_bytes, dst_width_pixels, dst_height_pixels,
		dx_abs, dy_abs, pixel_width, pixel_height,
		3);
}

BOOL ST_Blitter_Planar_Rect_Blit_Or(
	const uint8_t *src_root,
	int src_row_bytes,
	int src_width_pixels,
	int src_height_pixels,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Planar_Rect_Blit_With_Op(
		src_root, src_row_bytes, src_width_pixels, src_height_pixels,
		sx_abs, sy_abs, dst_root, dst_row_bytes, dst_width_pixels, dst_height_pixels,
		dx_abs, dy_abs, pixel_width, pixel_height,
		7);
}

BOOL ST_Blitter_Mask_And_Planar_Rect(
	const uint8_t *mask_root,
	int mask_row_bytes,
	int mask_width_pixels,
	int mask_height_pixels,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dst_width_pixels,
	int dst_height_pixels,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	if (!mask_root || !dst_root || !ST_Has_Blitter())
		return FALSE;

	if (pixel_width <= 0 || pixel_height <= 0
		|| sx_abs < 0 || sy_abs < 0 || dx_abs < 0 || dy_abs < 0
		|| mask_row_bytes <= 0 || dst_row_bytes <= 0
		|| mask_width_pixels <= 0 || mask_height_pixels <= 0
		|| dst_width_pixels <= 0 || dst_height_pixels <= 0
		|| mask_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| mask_width_pixels > ST_BLITTER_SHORT_MAX
		|| dst_width_pixels > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX
		|| sx_abs + pixel_width > mask_width_pixels
		|| dx_abs + pixel_width > dst_width_pixels
		|| sy_abs + pixel_height > mask_height_pixels
		|| dy_abs + pixel_height > dst_height_pixels) {
		return FALSE;
	}

	const short dst_word_left = (short)(dx_abs & ~15);
	const short dst_start = (short)(dx_abs - dst_word_left);
	const short words = (short)((dst_start + pixel_width + 15) >> 4);
	const short src_word_left = (short)(sx_abs & ~15);
	const short src_words = (short)(((sx_abs & 15) + pixel_width + 15) >> 4);
	if (words <= 0 || src_word_left < 0 || (src_word_left + src_words * 16) > mask_width_pixels) {
		return FALSE;
	}

	const unsigned char skew_low = (unsigned char)(((unsigned)(dx_abs & 15u) + 16u - (unsigned)(sx_abs & 15u)) % 16u);
	unsigned char sm = (unsigned char)(sx_abs & 15u);
	unsigned char dm = (unsigned char)(dx_abs & 15u);
	short skew_idx = (sm > dm) ? 1 : 0;
	const short src_span_m1 = (short)(((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4));
	const short dst_span_m1 = (short)(((dx_abs + pixel_width - 1) >> 4) - (dx_abs >> 4));
	if (dst_span_m1 == 0)
		skew_idx += 4;
	if (src_span_m1 == dst_span_m1)
		skew_idx += 2;
	if (skew_idx > 7)
		skew_idx &= 7;

	const short start = (short)(dx_abs & 15);
	const short end = (short)((dx_abs + pixel_width - 1) & 15);
	unsigned short endmask1 = (unsigned short)(0xFFFFu >> start);
	unsigned short endmask3 = (unsigned short)(0xFFFFu << (15 - end));
	unsigned short endmask2 = 0xFFFFu;
	if (words == 1) {
		endmask1 = (unsigned short)(endmask1 & endmask3);
		endmask2 = endmask1;
		endmask3 = endmask1;
	}

	const uint8_t *src = mask_root + (size_t)sy_abs * (size_t)mask_row_bytes
		+ (size_t)((src_word_left >> 4) * 2);
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)((dst_word_left >> 4) * 8);
	/*
	 * Left-edge clipping can make the source start farther into its first word
	 * than the destination. Use the standard FXSR/NFSR table, and let the 1bpp
	 * helper compensate Src_Yinc for the extra/suppressed source reads.
	 */
	const unsigned char skew_reg = (unsigned char)(skew_low | k_skew_fxsr_nfsr[skew_idx]);

	for (short pl = 0; pl < 4; ++pl) {
		ST_Blit_And_Mask_To_Plane_Skew_Masked(
			src,
			dst,
			(short)mask_row_bytes,
			(short)dst_row_bytes,
			words,
			(short)pixel_height,
			pl,
			skew_reg,
			FALSE,
			FALSE,
			endmask1,
			endmask2,
			endmask3);
	}
	return TRUE;
}
