/*
 * ST hardware blitter: planar rectangle copy with skew/masks.
 * Forward skew/endmask/X_Count follow Atari BLIT_iT (1987 Blitter manual, Appendix A).
 */

#include "st_blitter_blit.h"

#include "st_cache.h"
#include "st_frame_meter.h"
#include <stddef.h>

static_assert(sizeof(void *) == 4, "ST_Blitter address fields require 32-bit pointers");
static_assert(sizeof(ST_Blitter) == 30, "ST_Blitter must match $FF8A20..$FF8A3D");
static_assert(offsetof(ST_Blitter, src_x_inc) == 0, "ST_Blitter src_x_inc offset");
static_assert(offsetof(ST_Blitter, src_y_inc) == 2, "ST_Blitter src_y_inc offset");
static_assert(offsetof(ST_Blitter, src_addr) == 4, "ST_Blitter src_addr offset");
static_assert(offsetof(ST_Blitter, endmask1) == 8, "ST_Blitter endmask1 offset");
static_assert(offsetof(ST_Blitter, endmask2) == 10, "ST_Blitter endmask2 offset");
static_assert(offsetof(ST_Blitter, endmask3) == 12, "ST_Blitter endmask3 offset");
static_assert(offsetof(ST_Blitter, dst_x_inc) == 14, "ST_Blitter dst_x_inc offset");
static_assert(offsetof(ST_Blitter, dst_y_inc) == 16, "ST_Blitter dst_y_inc offset");
static_assert(offsetof(ST_Blitter, dst_addr) == 18, "ST_Blitter dst_addr offset");
static_assert(offsetof(ST_Blitter, x_count) == 22, "ST_Blitter x_count offset");
static_assert(offsetof(ST_Blitter, y_count) == 24, "ST_Blitter y_count offset");
static_assert(offsetof(ST_Blitter, hop) == 26, "ST_Blitter hop offset");
static_assert(offsetof(ST_Blitter, op) == 27, "ST_Blitter op offset");
static_assert(offsetof(ST_Blitter, ctrl) == 28, "ST_Blitter ctrl offset");
static_assert(offsetof(ST_Blitter, skew) == 29, "ST_Blitter skew offset");

#define g_Blitter (*(volatile ST_Blitter *)0xFFFF8A20UL)

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

void ST_Blitter_Await(void)
{
	while ((g_Blitter.ctrl & 0x80u) != 0) {
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
	ST_BLITTER_SHORT_MAX = 32767,
	ST_BLIT_CTRL_START = 0x80u,
	ST_BLIT_CTRL_START_HOG = 0xC0u,
	/* Non-overlap blits at or below this size use bus-hogging mode (~10% faster). */
	ST_BLIT_HOG_MAX_DIMENSION = 32
};

/*
 * HOG (bus-hogging) for short blits only. Overlap / self-blits (scroll) stay
 * cooperative so the CPU can service interrupts during long transfers.
 */
static BOOL ST_Blit_Should_Use_Hog(int pixel_width, int pixel_height, BOOL same_surface)
{
	if (same_surface || pixel_width <= 0 || pixel_height <= 0) {
		return FALSE;
	}
	return (pixel_width <= ST_BLIT_HOG_MAX_DIMENSION
		&& pixel_height <= ST_BLIT_HOG_MAX_DIMENSION);
}

typedef struct {
	const uint8_t *src_plane0;
	uint8_t *dst_plane0;
	BOOL src_addr_per_plane;
} ST_Blit_Job;

static BOOL ST_Blitter_Prepare_Impl(
	volatile ST_Blitter *blitter,
	ST_Blit_Job *job,
	const uint8_t *src_base,
	uint8_t *dst_base,
	short src_row_bytes,
	short dst_row_bytes,
	int sx_abs,
	int dx_abs,
	int pixel_width,
	int pixel_height,
	short src_x_inc,
	short dst_x_inc,
	short src_word_bytes,
	short dst_word_bytes,
	BOOL reverse_x,
	BOOL reverse_y,
	BOOL src_addr_per_plane)
{
	if (!blitter || !job || !src_base || !dst_base || pixel_width <= 0 || pixel_height <= 0) {
		return FALSE;
	}

	const short dst_words = (short)(((dx_abs + pixel_width - 1) >> 4) - (dx_abs >> 4) + 1);
	const short src_words = (short)(((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4) + 1);
	if (dst_words <= 0 || src_words <= 0) {
		return FALSE;
	}

	const unsigned char sm = (unsigned char)(sx_abs & 15u);
	const unsigned char dm = (unsigned char)(dx_abs & 15u);
	const unsigned char skew_low =
		(unsigned char)(((unsigned)dm + 16u - (unsigned)sm) % 16u);

	short skew_idx = (sm > dm) ? 1 : 0;
	if (src_words == dst_words) {
		skew_idx += 2;
	}
	if (dst_words == 1) {
		skew_idx += 4;
	}
	if (reverse_x && sm != dm) {
		skew_idx ^= 1;
	}
	skew_idx &= 7;

	unsigned short endmask1 = (unsigned short)(0xFFFFu >> dm);
	unsigned short endmask3 = (unsigned short)(0xFFFFu << (15 - ((dx_abs + pixel_width - 1) & 15)));
	if (dst_words == 1) {
		endmask1 = (unsigned short)(endmask1 & endmask3);
		endmask3 = endmask1;
	} else if (reverse_x) {
		const unsigned short t = endmask1;
		endmask1 = endmask3;
		endmask3 = t;
	}

	const unsigned char skew_reg =
		(unsigned char)(skew_low | k_skew_fxsr_nfsr[skew_idx]);
	const unsigned char skew_out =
		(src_words == 1 && dst_words == 1 && skew_low != 0)
		? (unsigned char)(skew_low | ST_BLIT_SKEW_FXSR)
		: skew_reg;

	if (reverse_x) {
		src_x_inc = (short)-src_x_inc;
		dst_x_inc = (short)-dst_x_inc;
	}
	if (dst_words == 1 && src_words == 1 && (skew_out & 0x0Fu) != 0u) {
		src_x_inc = 0;
		dst_x_inc = 0;
	}

	const short src_row = reverse_y ? (short)-src_row_bytes : src_row_bytes;
	const short dst_row = reverse_y ? (short)-dst_row_bytes : dst_row_bytes;

	blitter->src_x_inc = (uint16_t)src_x_inc;
	blitter->src_y_inc = (uint16_t)(src_row - (src_words - 1) * src_x_inc);
	blitter->endmask1 = endmask1;
	blitter->endmask2 = (dst_words == 1) ? endmask1 : 0xFFFFu;
	blitter->endmask3 = endmask3;
	blitter->dst_x_inc = (uint16_t)dst_x_inc;
	blitter->dst_y_inc = (uint16_t)(dst_row - (dst_words - 1) * dst_x_inc);
	blitter->x_count = (uint16_t)dst_words;
	blitter->skew = skew_out;

	const uint8_t *src_plane0 = src_base
		+ (reverse_y ? (size_t)(pixel_height - 1) * (size_t)src_row_bytes : 0u)
		+ (reverse_x ? (size_t)(src_words - 1) * (size_t)src_word_bytes : 0u);
	uint8_t *dst_plane0 = dst_base
		+ (reverse_y ? (size_t)(pixel_height - 1) * (size_t)dst_row_bytes : 0u)
		+ (reverse_x ? (size_t)(dst_words - 1) * (size_t)dst_word_bytes : 0u);
	blitter->src_addr = (void *)src_plane0;
	blitter->dst_addr = dst_plane0;

	job->src_plane0 = src_plane0;
	job->dst_plane0 = dst_plane0;
	job->src_addr_per_plane = src_addr_per_plane;
	return TRUE;
}

static BOOL ST_Blitter_Prepare_88(
	volatile ST_Blitter *blitter,
	ST_Blit_Job *job,
	const uint8_t *src_base,
	uint8_t *dst_base,
	short src_row_bytes,
	short dst_row_bytes,
	int sx_abs,
	int dx_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Prepare_Impl(
		blitter,
		job,
		src_base,
		dst_base,
		src_row_bytes,
		dst_row_bytes,
		sx_abs,
		dx_abs,
		pixel_width,
		pixel_height,
		8,
		8,
		8,
		8,
		FALSE,
		FALSE,
		TRUE);
}

static BOOL ST_Blitter_Prepare_88_Scroll(
	volatile ST_Blitter *blitter,
	ST_Blit_Job *job,
	const uint8_t *src_base,
	uint8_t *dst_base,
	short src_row_bytes,
	short dst_row_bytes,
	int sx_abs,
	int sy_abs,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	const BOOL reverse_x = (dx_abs > sx_abs);
	const BOOL reverse_y = (dy_abs > sy_abs);
	return ST_Blitter_Prepare_Impl(
		blitter,
		job,
		src_base,
		dst_base,
		src_row_bytes,
		dst_row_bytes,
		sx_abs,
		dx_abs,
		pixel_width,
		pixel_height,
		8,
		8,
		8,
		8,
		reverse_x,
		reverse_y,
		TRUE);
}

static BOOL ST_Blitter_Prepare_28(
	volatile ST_Blitter *blitter,
	ST_Blit_Job *job,
	const uint8_t *src_base,
	uint8_t *dst_base,
	short src_row_bytes,
	short dst_row_bytes,
	int sx_abs,
	int dx_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blitter_Prepare_Impl(
		blitter,
		job,
		src_base,
		dst_base,
		src_row_bytes,
		dst_row_bytes,
		sx_abs,
		dx_abs,
		pixel_width,
		pixel_height,
		2,
		8,
		2,
		8,
		FALSE,
		FALSE,
		FALSE);
}

static void ST_Blitter_Execute(
	volatile ST_Blitter *blitter,
	short lines,
	unsigned char blit_op,
	BOOL hog_mode)
{
	blitter->y_count = (uint16_t)lines;
	blitter->hop = 2;
	blitter->op = blit_op;
	blitter->ctrl = hog_mode ? ST_BLIT_CTRL_START_HOG : ST_BLIT_CTRL_START;
}

/*
 * BLIT_iT next_plane loop: fixed registers once, then per plane only
 * Src_Addr, Dst_Addr, and Y_Count (hardware decrements Y_Count to zero each pass).
 * X_Count is set once — it auto-reloads at the end of each blitted line.
 * Wait before and after every kick (required for mask AND correctness).
 */
static void ST_Blit_Run_4_Planes(
	volatile ST_Blitter *blitter,
	const ST_Blit_Job *job,
	short lines,
	unsigned char blit_op,
	BOOL hog_mode)
{
	if (!blitter || !job || lines <= 0) {
		return;
	}

	for (short pl = 0; pl < 4; ++pl) {
		ST_Blitter_Await();
		blitter->src_addr = (void *)(job->src_plane0
			+ (job->src_addr_per_plane ? (size_t)pl * 2u : 0u));
		blitter->dst_addr = job->dst_plane0 + (size_t)pl * 2u;
		ST_Blitter_Execute(blitter, lines, blit_op, hog_mode);
	}
	ST_Blitter_Await();
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
	const BOOL hog_mode = ST_Blit_Should_Use_Hog(pixel_width, pixel_height, same_surface);
	const uint8_t *src = src_root + (size_t)sy_abs * (size_t)src_row_bytes
		+ (size_t)(sx_abs >> 4) * 8;
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)(dx_abs >> 4) * 8;
	ST_Blit_Job job;

	ST_Blit_Sync_Cache_Before(
		src_root, src_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs);
	ST_FRAME_BAR_BLIT_BEGIN();
	ST_Blitter_Await();
	const BOOL prepared = same_surface
		? ST_Blitter_Prepare_88_Scroll(
			&g_Blitter,
			&job,
			src,
			dst,
			(short)src_row_bytes,
			(short)dst_row_bytes,
			sx_abs,
			sy_abs,
			dx_abs,
			dy_abs,
			pixel_width,
			(short)pixel_height)
		: ST_Blitter_Prepare_88(
			&g_Blitter,
			&job,
			src,
			dst,
			(short)src_row_bytes,
			(short)dst_row_bytes,
			sx_abs,
			dx_abs,
			pixel_width,
			(short)pixel_height);
	if (!prepared) {
		ST_FRAME_BAR_BLIT_END();
		return FALSE;
	}
	ST_Blit_Run_4_Planes(
		&g_Blitter,
		&job,
		(short)pixel_height,
		blit_op,
		hog_mode);
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

static BOOL ST_Blitter_Mask_And_Planar_Rect_With_Op(
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

	const BOOL hog_mode = ST_Blit_Should_Use_Hog(pixel_width, pixel_height, FALSE);

	const short src_word_left = (short)(sx_abs & ~15);
	const short dst_word_left = (short)(dx_abs & ~15);
	const uint8_t *src = mask_root + (size_t)sy_abs * (size_t)mask_row_bytes
		+ (size_t)((src_word_left >> 4) * 2);
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)((dst_word_left >> 4) * 8);
	ST_Blit_Job job;

	ST_Blit_Sync_Cache_Before(
		mask_root, mask_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs);
	ST_FRAME_BAR_BLIT_BEGIN();
	ST_Blitter_Await();
	if (!ST_Blitter_Prepare_28(
			&g_Blitter,
			&job,
			src,
			dst,
			(short)mask_row_bytes,
			(short)dst_row_bytes,
			sx_abs,
			dx_abs,
			pixel_width,
			(short)pixel_height)) {
		ST_FRAME_BAR_BLIT_END();
		return FALSE;
	}
	ST_Blit_Run_4_Planes(
		&g_Blitter,
		&job,
		(short)pixel_height,
		1,
		hog_mode);
	ST_FRAME_BAR_BLIT_END();
	ST_Blit_Sync_Cache_After(
		mask_root, mask_row_bytes, sy_abs, pixel_height,
		dst_root, dst_row_bytes, dy_abs,
		FALSE);
	return TRUE;
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
	return ST_Blitter_Mask_And_Planar_Rect_With_Op(
		mask_root, mask_row_bytes,
		sx_abs, sy_abs, dst_root, dst_row_bytes,
		dx_abs, dy_abs, pixel_width, pixel_height);
}
