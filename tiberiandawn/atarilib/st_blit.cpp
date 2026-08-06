/*
 * ST planar blit: prepare, backend dispatch, public API.
 * Forward skew/endmask/X_Count follow Atari BLIT_iT (1987 Blitter manual, Appendix A).
 */

#include "st_blit.h"

#include "drawbuff.h"
#include "st_frame_meter.h"

#include <stddef.h>
#include <stdint.h>

enum {
	ST_BLIT_SKEW_NFSR = 0x40u,
	ST_BLIT_SKEW_FXSR = 0x80u
};

enum {
	ST_BLITTER_SHORT_MAX = 32767,
	ST_BLIT_HOG_MAX_DIMENSION = 32
};

static const unsigned char k_skew_fxsr_nfsr[8] = {
	ST_BLIT_SKEW_NFSR,
	ST_BLIT_SKEW_FXSR,
	0x00u,
	ST_BLIT_SKEW_NFSR + ST_BLIT_SKEW_FXSR,
	0x00u,
	ST_BLIT_SKEW_FXSR,
	0x00u,
	0x00u
};

static bool ST_Blit_Can_Use_Hardware(void)
{
	/* Same policy as g_blit_backend; kept for the HW two-pass merge path. */
	return AllowHardwareBlitFills;
}

static bool ST_Blit_Should_Use_Hog(int pixel_width, int pixel_height)
{
	if (pixel_width <= 0 || pixel_height <= 0) {
		return false;
	}
	return (pixel_width <= ST_BLIT_HOG_MAX_DIMENSION
		&& pixel_height <= ST_BLIT_HOG_MAX_DIMENSION);
}

static bool ST_Blit_Prepare_Impl(
	ST_Blitter *regs,
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
	bool reverse_x,
	bool reverse_y,
	bool src_addr_per_plane)
{
	if (!regs || !job || !src_base || !dst_base || pixel_width <= 0 || pixel_height <= 0) {
		return false;
	}

	const short dst_words = (short)(((dx_abs + pixel_width - 1) >> 4) - (dx_abs >> 4) + 1);
	const short src_words = (short)(((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4) + 1);
	if (dst_words <= 0 || src_words <= 0) {
		return false;
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
	unsigned short endmask3 =
		(unsigned short)(0xFFFFu << (15 - ((dx_abs + pixel_width - 1) & 15)));
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

	regs->src_x_inc = src_x_inc;
	regs->src_y_inc = (short)(src_row - (src_words - 1) * src_x_inc);
	regs->endmask1 = endmask1;
	regs->endmask2 = (dst_words == 1) ? endmask1 : 0xFFFFu;
	regs->endmask3 = endmask3;
	regs->dst_x_inc = dst_x_inc;
	regs->dst_y_inc = (short)(dst_row - (dst_words - 1) * dst_x_inc);
	regs->x_count = (uint16_t)dst_words;
	regs->skew = skew_out;

	const uint8_t *src_plane0 = src_base
		+ (reverse_y ? (size_t)(pixel_height - 1) * (size_t)src_row_bytes : 0u)
		+ (reverse_x ? (size_t)(src_words - 1) * (size_t)src_word_bytes : 0u);
	uint8_t *dst_plane0 = dst_base
		+ (reverse_y ? (size_t)(pixel_height - 1) * (size_t)dst_row_bytes : 0u)
		+ (reverse_x ? (size_t)(dst_words - 1) * (size_t)dst_word_bytes : 0u);
	regs->src_addr = (void *)src_plane0;
	regs->dst_addr = dst_plane0;

	job->src_plane0 = src_plane0;
	job->dst_plane0 = dst_plane0;
	job->src_addr_per_plane = src_addr_per_plane;
	return true;
}

static bool ST_Blit_Prepare_88(
	ST_Blitter *regs,
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
	return ST_Blit_Prepare_Impl(
		regs,
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
		false,
		false,
		true);
}

static bool ST_Blit_Prepare_88_Scroll(
	ST_Blitter *regs,
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
	const bool reverse_x = (dx_abs > sx_abs);
	const bool reverse_y = (dy_abs > sy_abs);
	return ST_Blit_Prepare_Impl(
		regs,
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
		true);
}

static bool ST_Blit_Prepare_28(
	ST_Blitter *regs,
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
	return ST_Blit_Prepare_Impl(
		regs,
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
		false,
		false,
		false);
}

static ST_Blit_Backend *g_blit_backend;

void ST_Blit_Init_Backend(void)
{
	g_blit_backend = AllowHardwareBlitFills
		? static_cast<ST_Blit_Backend *>(&ST_Blit_HW_Backend())
		: static_cast<ST_Blit_Backend *>(&ST_Blit_Soft_Backend());
}

static ST_Blit_Backend &ST_Blit_Active_Backend(void)
{
	if (!g_blit_backend) {
		/* Soft until Probe / ST_Blit_Init_Backend (tests, early callers). */
		g_blit_backend = &ST_Blit_Soft_Backend();
	}
	return *g_blit_backend;
}

void ST_Blit_Backend::Run_Planes(const ST_Blitter &plan, const ST_Blit_Job &job,
    uint16_t lines, bool hog)
{
	/* Only here does the plan reach real registers. */
	Program(plan);
	for (int pl = 0; pl < 4; ++pl) {
		const void *src_addr = job.src_plane0
			+ (job.src_addr_per_plane ? (size_t)pl * 2u : 0u);
		uint8_t *const dst_addr = job.dst_plane0 + (size_t)pl * 2u;
		Execute(hog, lines, (void *)src_addr, dst_addr);
	}
}

void ST_Blit_Backend::Program(const ST_Blitter &plan)
{
	volatile ST_Blitter &r = regs_;
	r.src_x_inc = plan.src_x_inc;
	r.src_y_inc = plan.src_y_inc;
	r.src_addr = plan.src_addr;
	r.endmask1 = plan.endmask1;
	r.endmask2 = plan.endmask2;
	r.endmask3 = plan.endmask3;
	r.dst_x_inc = plan.dst_x_inc;
	r.dst_y_inc = plan.dst_y_inc;
	r.dst_addr = plan.dst_addr;
	r.x_count = plan.x_count;
	r.hop = plan.hop;
	r.op = plan.op;
	r.skew = plan.skew;
}

static void ST_Blit_Run_4_Planes(
	ST_Blit_Backend &backend,
	const ST_Blitter &plan,
	const ST_Blit_Job &job,
	short lines,
	bool hog)
{
	if (lines <= 0) {
		return;
	}

	backend.Run_Planes(plan, job, (uint16_t)lines, hog);
	backend.Await();
}

static BOOL ST_Blit_Planar_Rect_With_Op(
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
	uint8_t blit_op)
{
	if (!src_root || !dst_root) {
		return FALSE;
	}

	if (pixel_width <= 0 || pixel_height <= 0
		|| src_row_bytes <= 0 || dst_row_bytes <= 0
		|| src_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX) {
		return FALSE;
	}

	const bool same_surface = (src_root == dst_root);
	const bool hog = ST_Blit_Should_Use_Hog(pixel_width, pixel_height);
	const uint8_t *src = src_root + (size_t)sy_abs * (size_t)src_row_bytes
		+ (size_t)(sx_abs >> 4) * 8;
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)(dx_abs >> 4) * 8;
	ST_Blit_Job job;

	ST_FRAME_BAR_BLIT_BEGIN();

	ST_Blit_Backend &backend = ST_Blit_Active_Backend();
	ST_Blitter plan{};
	ST_Blitter &regs = plan;
	backend.Await();

	const bool prepared = same_surface
		? ST_Blit_Prepare_88_Scroll(
			&regs,
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
		: ST_Blit_Prepare_88(
			&regs,
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

	regs.hop = 2;
	regs.op = blit_op;
	ST_Blit_Run_4_Planes(
		backend,
		plan,
		job,
		(short)pixel_height,
		hog);

	ST_FRAME_BAR_BLIT_END();
	return TRUE;
}

BOOL ST_Blit_Planar_Screen_Rect_Blit(
	const uint8_t *src_root,
	uint8_t *dst_root,
	int sx_abs,
	int sy_abs,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	return ST_Blit_Planar_Rect_Blit(
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

BOOL ST_Blit_Planar_Rect_Blit(
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
	return ST_Blit_Planar_Rect_With_Op(
		src_root, src_row_bytes,
		sx_abs, sy_abs, dst_root, dst_row_bytes,
		dx_abs, dy_abs, pixel_width, pixel_height,
		3);
}

BOOL ST_Blit_Planar_Rect_Blit_Or(
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
	return ST_Blit_Planar_Rect_With_Op(
		src_root, src_row_bytes,
		sx_abs, sy_abs, dst_root, dst_row_bytes,
		dx_abs, dy_abs, pixel_width, pixel_height,
		7);
}

BOOL ST_Blit_Mask_And_Planar_Rect(
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
	if (!mask_root || !dst_root) {
		return FALSE;
	}

	if (pixel_width <= 0 || pixel_height <= 0
		|| mask_row_bytes <= 0 || dst_row_bytes <= 0
		|| mask_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX) {
		return FALSE;
	}

	const bool hog = ST_Blit_Should_Use_Hog(pixel_width, pixel_height);
	const short src_word_left = (short)(sx_abs & ~15);
	const short dst_word_left = (short)(dx_abs & ~15);
	const uint8_t *src = mask_root + (size_t)sy_abs * (size_t)mask_row_bytes
		+ (size_t)((src_word_left >> 4) * 2);
	uint8_t *dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)((dst_word_left >> 4) * 8);
	ST_Blit_Job job;

	ST_FRAME_BAR_BLIT_BEGIN();

	ST_Blit_Backend &backend = ST_Blit_Active_Backend();
	ST_Blitter plan{};
	ST_Blitter &regs = plan;
	backend.Await();

	if (!ST_Blit_Prepare_28(
			&regs,
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

	regs.hop = 2;
	regs.op = 1;
	ST_Blit_Run_4_Planes(
		backend,
		plan,
		job,
		(short)pixel_height,
		hog);

	ST_FRAME_BAR_BLIT_END();
	return TRUE;
}

BOOL ST_Blit_Mask_Merge_Planar_Rect(
	const uint8_t *mask_root,
	int mask_row_bytes,
	const uint8_t *planar_root,
	int planar_row_bytes,
	int sx_abs,
	int sy_abs,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height)
{
	if (!mask_root || !planar_root || !dst_root) {
		return FALSE;
	}
	if (pixel_width <= 0 || pixel_height <= 0
		|| mask_row_bytes <= 0 || planar_row_bytes <= 0 || dst_row_bytes <= 0
		|| mask_row_bytes > ST_BLITTER_SHORT_MAX
		|| planar_row_bytes > ST_BLITTER_SHORT_MAX
		|| dst_row_bytes > ST_BLITTER_SHORT_MAX
		|| pixel_height > ST_BLITTER_SHORT_MAX) {
		return FALSE;
	}

	const uint8_t *const mask_src = mask_root + (size_t)sy_abs * (size_t)mask_row_bytes
		+ (size_t)((sx_abs >> 4) * 2);
	const uint8_t *const planar_src = planar_root + (size_t)sy_abs * (size_t)planar_row_bytes
		+ (size_t)(sx_abs >> 4) * 8;
	uint8_t *const dst = dst_root + (size_t)dy_abs * (size_t)dst_row_bytes
		+ (size_t)(dx_abs >> 4) * 8;

	const bool two_pass = ST_Blit_Can_Use_Hardware();

	if (!two_pass) {
		/*
		 * One prepare, not two. Of everything ST_Blit_Prepare_Impl computes, only
		 * src_x_inc, src_y_inc and (for reverse only) src_plane0 depend on the
		 * source stride — endmasks, skew, x_count and the whole destination side
		 * are identical because both streams sit at the same sx/dx. This path is
		 * always forward, so the mask stream reduces to two derived values.
		 */
		ST_Blitter planar_regs;
		ST_Blit_Job planar_job;

		const bool ok = ST_Blit_Prepare_88(&planar_regs, &planar_job, planar_src, dst,
			(short)planar_row_bytes, (short)dst_row_bytes,
			sx_abs, dx_abs, pixel_width, (short)pixel_height);

		const bool mergeable = ok
			&& planar_regs.src_x_inc == 8
			&& planar_regs.dst_x_inc == 8
			&& planar_job.src_addr_per_plane;

		/*
		 * Both increments zeroed is the degenerate single-word geometry. It has
		 * its own merged loop; without it the two-pass fallback below would run
		 * eight single-plane passes instead of one.
		 */
		const bool degenerate = ok
			&& planar_regs.src_x_inc == 0
			&& planar_regs.dst_x_inc == 0
			&& planar_regs.x_count == 1
			&& planar_job.src_addr_per_plane
			/* Long accesses on both sides: on a 68000 an odd address
			   here would be an address error, not just slow. */
			&& ((((unsigned long)(uintptr_t)planar_job.src_plane0
			      | (unsigned long)(uintptr_t)planar_job.dst_plane0
			      | (unsigned long)(unsigned short)planar_regs.src_y_inc
			      | (unsigned long)(unsigned short)planar_regs.dst_y_inc)
			     & 3u) == 0u);

		if (degenerate) {
			ST_FRAME_BAR_BLIT_BEGIN();

			ST_Soft_Blit_Merge_Degenerate(
				mask_src,
				(int16_t)mask_row_bytes,
				planar_job.src_plane0,
				planar_regs.src_y_inc,
				planar_job.dst_plane0,
				planar_regs.dst_y_inc,
				(uint16_t)pixel_height,
				planar_regs.endmask1,
				(unsigned)(planar_regs.skew & 15u));

			ST_FRAME_BAR_BLIT_END();
			return TRUE;
		}

		if (mergeable) {
			const int src_words =
				((sx_abs + pixel_width - 1) >> 4) - (sx_abs >> 4) + 1;
			const uint8_t *const mask_plane0 = mask_src;
			const int16_t mask_y_inc =
				(int16_t)(mask_row_bytes - (src_words - 1) * 2);

			ST_FRAME_BAR_BLIT_BEGIN();

			ST_Soft_Blit_Mask_Merge(
				mask_plane0,
				mask_y_inc,
				planar_job.src_plane0,
				planar_regs.src_y_inc,
				planar_job.dst_plane0,
				planar_regs.dst_y_inc,
				planar_regs.x_count,
				(uint16_t)pixel_height,
				planar_regs.endmask1,
				planar_regs.endmask2,
				planar_regs.endmask3,
				(unsigned)(planar_regs.skew & 15u),
				(planar_regs.skew & 0x80u) != 0,
				(planar_regs.skew & 0x40u) != 0);

			ST_FRAME_BAR_BLIT_END();
			return TRUE;
		}
	}

	/* Hardware, or a register image the merged loop does not cover. */
	if (!ST_Blit_Mask_And_Planar_Rect(mask_root, mask_row_bytes, sx_abs, sy_abs,
		    dst_root, dst_row_bytes, dx_abs, dy_abs, pixel_width, pixel_height)) {
		return FALSE;
	}
	return ST_Blit_Planar_Rect_Blit_Or(planar_root, planar_row_bytes, sx_abs, sy_abs,
		dst_root, dst_row_bytes, dx_abs, dy_abs, pixel_width, pixel_height);
}
