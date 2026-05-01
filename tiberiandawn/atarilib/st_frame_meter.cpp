/*
 * ST frame profiler: rolling ~1 second (_hz200) averages, tab-strip bar display.
 */
#include "function.h"

#if defined(ATARI_ST) && defined(ST_FRAME_BAR_PROFILE)

#include "gbuffer.h"
#include "wwstd.h"

#ifndef ST_FRAME_BAR_HZ200_TICKS_ONE_SECOND
#define ST_FRAME_BAR_HZ200_TICKS_ONE_SECOND 200
#endif

#ifndef ST_FRAME_BAR_HZ200_TICKS_FULL_BAR
#define ST_FRAME_BAR_HZ200_TICKS_FULL_BAR 50
#endif

enum { HZ200_ADDR = 0x4BA };

static inline unsigned long St_FrameMeter_ReadHz200(void)
{
	return *(volatile unsigned long *)HZ200_ADDR;
}

static unsigned long s_frame_hz0;

static unsigned long s_render_sum_this_frame;
static unsigned int s_render_depth;
static unsigned long s_render_hz0;

static unsigned long s_theme_sum_this_frame;
static unsigned int s_theme_depth;
static unsigned long s_theme_hz0;

static unsigned long s_logic_sum_this_frame;
static unsigned int s_logic_depth;
static unsigned long s_logic_hz0;

static unsigned long s_c2p_sum_this_frame;
static unsigned int s_c2p_depth;
static unsigned long s_c2p_hz0;

static unsigned long s_blit_sum_this_frame;
static unsigned int s_blit_depth;
static unsigned long s_blit_hz0;

static unsigned long s_period_hz_start;
static unsigned long s_period_sum_frame;
static unsigned long s_period_sum_render;
static unsigned long s_period_sum_logic;
static unsigned long s_period_sum_theme;
static unsigned long s_period_sum_c2p;
static unsigned long s_period_sum_blit;
static unsigned long s_period_frames;

static unsigned long s_disp_frame_avg;
static unsigned long s_disp_render_avg;
static unsigned long s_disp_logic_avg;
static unsigned long s_disp_theme_avg;
static unsigned long s_disp_c2p_avg;
static unsigned long s_disp_blit_avg;

static void St_FrameMeter_MaybeRollPeriod(unsigned long hz_now)
{
	if (s_period_hz_start == 0) {
		s_period_hz_start = hz_now;
		return;
	}

	unsigned long elapsed = hz_now - s_period_hz_start;
	if (elapsed < (unsigned long)ST_FRAME_BAR_HZ200_TICKS_ONE_SECOND) {
		return;
	}

	if (s_period_frames != 0) {
		s_disp_frame_avg = s_period_sum_frame / s_period_frames;
		s_disp_render_avg = s_period_sum_render / s_period_frames;
		s_disp_logic_avg = s_period_sum_logic / s_period_frames;
		s_disp_theme_avg = s_period_sum_theme / s_period_frames;
		s_disp_c2p_avg = s_period_sum_c2p / s_period_frames;
		s_disp_blit_avg = s_period_sum_blit / s_period_frames;
		Map.Redraw_Tab();
	}

	s_period_sum_frame = 0;
	s_period_sum_render = 0;
	s_period_sum_logic = 0;
	s_period_sum_theme = 0;
	s_period_sum_c2p = 0;
	s_period_sum_blit = 0;
	s_period_frames = 0;
	s_period_hz_start = hz_now;
}

void StFrameMeter_FrameBegin(void)
{
	s_frame_hz0 = St_FrameMeter_ReadHz200();

	s_render_sum_this_frame = 0;
	s_theme_sum_this_frame = 0;
	s_logic_sum_this_frame = 0;
	s_c2p_sum_this_frame = 0;
	s_blit_sum_this_frame = 0;
}

void StFrameMeter_FrameEnd(void)
{
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	unsigned long frame_dt = hz1 - s_frame_hz0;

	s_period_sum_frame += frame_dt;
	s_period_sum_render += s_render_sum_this_frame;
	s_period_sum_logic += s_logic_sum_this_frame;
	s_period_sum_theme += s_theme_sum_this_frame;
	s_period_sum_c2p += s_c2p_sum_this_frame;
	s_period_sum_blit += s_blit_sum_this_frame;
	s_period_frames++;

	St_FrameMeter_MaybeRollPeriod(hz1);
}

void StFrameMeter_RenderBegin(void)
{
	if (s_render_depth++ == 0) {
		s_render_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_RenderEnd(void)
{
	if (--s_render_depth != 0) {
		return;
	}
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	unsigned long dt = hz1 - s_render_hz0;
	s_render_sum_this_frame += dt;
}

void StFrameMeter_ThemeBegin(void)
{
	if (s_theme_depth++ == 0) {
		s_theme_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_ThemeEnd(void)
{
	if (--s_theme_depth != 0) {
		return;
	}
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	s_theme_sum_this_frame += hz1 - s_theme_hz0;
}

void StFrameMeter_LogicBegin(void)
{
	if (s_logic_depth++ == 0) {
		s_logic_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_LogicEnd(void)
{
	if (--s_logic_depth != 0) {
		return;
	}
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	s_logic_sum_this_frame += hz1 - s_logic_hz0;
}

void StFrameMeter_C2pBegin(void)
{
	if (s_c2p_depth++ == 0) {
		s_c2p_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_C2pEnd(void)
{
	if (--s_c2p_depth != 0) {
		return;
	}
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	s_c2p_sum_this_frame += hz1 - s_c2p_hz0;
}

void StFrameMeter_BlitterBegin(void)
{
	if (s_blit_depth++ == 0) {
		s_blit_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_BlitterEnd(void)
{
	if (--s_blit_depth != 0) {
		return;
	}
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	s_blit_sum_this_frame += hz1 - s_blit_hz0;
}

void StFrameMeter_Draw(GraphicViewPortClass *page, int width_factor)
{
	if (page == nullptr) {
		return;
	}

	int bw = 80 * width_factor;
	long scale = ST_FRAME_BAR_HZ200_TICKS_FULL_BAR;
	if (scale <= 0) {
		scale = 1;
	}

	int x0 = 80 * width_factor;
	/* ST 16-color tab UI: closest to magenta */
	const unsigned char k_frame_bar_magenta = (unsigned char)PURPLE;

	for (int row = 0; row < 4; ++row) {
		if (row == 1) {
			/*
			 * Graphics (render): LTGREY = total render. Overlays (left → right, Hz200 scale):
			 *   pink = C2P, purple = ST blitter (rect / mask).
			 */
			long span_r = ((long)(s_disp_render_avg * (unsigned long)bw) / scale);
			long span_c = ((long)(s_disp_c2p_avg * (unsigned long)bw) / scale);
			long span_b = ((long)(s_disp_blit_avg * (unsigned long)bw) / scale);
			if (span_r < 0) {
				continue;
			}
			if (span_c < 0) {
				span_c = 0;
			}
			if (span_b < 0) {
				span_b = 0;
			}
			int ir = (int)span_r;
			int ic = (int)span_c;
			int ib = (int)span_b;
			if (ir > bw) {
				ir = bw;
			}
			if (ic > ir) {
				ic = ir;
			}
			if (ib > ir - ic) {
				ib = ir - ic;
			}
			if (ir > 0) {
				page->Fill_Rect(x0, row, x0 + ir - 1, row, LTGREY);
			}
			if (ic > 0) {
				page->Fill_Rect(x0, row, x0 + ic - 1, row, PINK);
			}
			if (ib > 0) {
				page->Fill_Rect(x0 + ic, row, x0 + ic + ib - 1, row, k_frame_bar_magenta);
			}
			continue;
		}

		unsigned long ticks;
		unsigned char colour;

		switch (row) {
		case 0:
			ticks = s_disp_frame_avg;
			colour = WHITE;
			break;
		case 2:
			ticks = s_disp_logic_avg;
			colour = LTBLUE;
			break;
		case 3:
			ticks = s_disp_theme_avg;
			colour = YELLOW;
			break;
		default:
			continue;
		}

		long span_long = ((long)(ticks * (unsigned long)bw) / scale);
		if (span_long < 0) {
			continue;
		}
		int span = (int)span_long;
		if (span > bw) {
			span = bw;
		}
		if (span <= 0) {
			continue;
		}
		int xend = x0 + span - 1;
		page->Fill_Rect(x0, row, xend, row, colour);
	}
}

#endif /* ATARI_ST && ST_FRAME_BAR_PROFILE */
