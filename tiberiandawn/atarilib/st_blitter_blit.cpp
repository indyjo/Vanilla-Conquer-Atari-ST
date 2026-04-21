/*
 * ST hardware blitter: planar 320x200 framebuffer rectangle copy with skew/masks.
 */

#include "st_blitter_blit.h"

#include <stddef.h>

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

static void ST_Blit_Copy_Plane_Skew_Masked(
	const uint8_t *src_base, uint8_t *dst_base,
	int src_row_bytes, int dst_row_bytes,
	int words, int lines, int plane_idx,
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
	const int sx = reverse_x ? -8 : 8;
	const int dx = reverse_x ? -8 : 8;
	const int src_row_advance = reverse_y ? -src_row_bytes : src_row_bytes;
	const int dst_row_advance = reverse_y ? -dst_row_bytes : dst_row_bytes;
	const int src_y_inc = src_row_advance - (words - 1) * sx;
	const int dst_y_inc = dst_row_advance - (words - 1) * dx;
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
	*(volatile unsigned char *)0xFFFF8A3BUL = 3; /* OP: D = S */
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
	if (!src_root || !dst_root || !ST_Has_Blitter())
		return FALSE;

	if (pixel_width <= 0 || pixel_height <= 0
		|| sx_abs < 0 || sy_abs < 0 || dx_abs < 0 || dy_abs < 0
		|| src_row_bytes <= 0 || dst_row_bytes <= 0
		|| src_width_pixels <= 0 || src_height_pixels <= 0
		|| dst_width_pixels <= 0 || dst_height_pixels <= 0
		|| sx_abs + pixel_width > src_width_pixels
		|| dx_abs + pixel_width > dst_width_pixels
		|| sy_abs + pixel_height > src_height_pixels
		|| dy_abs + pixel_height > dst_height_pixels) {
		return FALSE;
	}

	const int dst_word_left = dx_abs & ~15;
	const int dst_start = dx_abs - dst_word_left;
	const int words = (dst_start + pixel_width + 15) >> 4;
	const int src_word_left = sx_abs & ~15;
	if (words <= 0 || src_word_left < 0 || (src_word_left + words * 16) > src_width_pixels) {
		return FALSE;
	}

	/* Skew nibble: (dst X mod 16 - src X mod 16) mod 16 (Atari manual). */
	const unsigned skew_low = ((unsigned)(dx_abs & 15u) + 16u - (unsigned)(sx_abs & 15u)) % 16u;
	unsigned sm = (unsigned)sx_abs & 15u;
	unsigned dm = (unsigned)dx_abs & 15u;
	int skew_idx = (sm > dm) ? 1 : 0;
	const int src_span_m1 = ((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4);
	const int dst_span_m1 = ((dx_abs + pixel_width - 1) >> 4) - (dx_abs >> 4);
	if (dst_span_m1 == 0)
		skew_idx += 4;
	if (src_span_m1 == dst_span_m1)
		skew_idx += 2;

	/*
	 * Same-surface overlap: negate X step only when dest is to the right of source,
	 * negate Y step only when dest is below source (standard BitBlt / VDI rules).
	 * Do not tie both to one row-major compare — a horizontal scroll must not set NFY,
	 * and a vertical scroll must not set reverse X, or the blitter walks the wrong edge.
	 * Needed before endmasks: negative X processes words right-to-left, so endmask1/3 swap.
	 */
	const BOOL same_surface = (src_root == dst_root);
	const BOOL reverse_x = same_surface && (dx_abs > sx_abs);
	const BOOL reverse_y = same_surface && (dy_abs > sy_abs);

	/*
	 * Skew FXSR/NFSR table matches forward (positive) X fetch. For negative X, flip the
	 * sm/dm branch bit so prefetch flags match reversed read order when src/dst are
	 * misaligned (skew nibble != 0).
	 */
	if (reverse_x && sm != dm)
		skew_idx ^= 1;
	if (skew_idx > 7)
		skew_idx &= 7;

	const int start = dx_abs & 15;
	const int end = (dx_abs + pixel_width - 1) & 15;
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
	unsigned char skew_flags = k_skew_fxsr_nfsr[skew_idx];
	/* Empirically validated by Test10 skew-flag sweep: sw<dw requires FXSR/NFSR=00. */
	if (src_span_m1 < dst_span_m1)
		skew_flags = 0x00u;
	const unsigned char skew_reg = (unsigned char)(skew_low | skew_flags);

	for (int pl = 0; pl < 4; ++pl) {
		ST_Blit_Copy_Plane_Skew_Masked(
			src,
			dst,
			src_row_bytes,
			dst_row_bytes,
			words,
			pixel_height,
			pl,
			skew_reg,
			reverse_x,
			reverse_y,
			endmask1,
			endmask2,
			endmask3);
	}
	return TRUE;
}
