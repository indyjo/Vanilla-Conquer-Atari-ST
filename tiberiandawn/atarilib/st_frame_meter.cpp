/*
 * ST frame profiler: rolling ~1 second (_hz200) averages, tab-strip bar display.
 */
#include "function.h"
#include "ATARILIB/st_frame_meter.h"

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

static unsigned long s_map_phase_hz0[ST_FM_MAP_PHASE_COUNT];
static unsigned int s_map_phase_depth[ST_FM_MAP_PHASE_COUNT];
static unsigned long s_map_phase_sum_this_frame[ST_FM_MAP_PHASE_COUNT];

static unsigned long s_period_hz_start;
static unsigned long s_period_sum_frame;
static unsigned long s_period_sum_render;
static unsigned long s_period_sum_logic;
static unsigned long s_period_sum_theme;
static unsigned long s_period_sum_map_phase[ST_FM_MAP_PHASE_COUNT];
static unsigned long s_period_frames;

static unsigned long s_disp_frame_avg;
static unsigned long s_disp_render_avg;
static unsigned long s_disp_logic_avg;
static unsigned long s_disp_theme_avg;
static unsigned long s_disp_map_phase_avg[ST_FM_MAP_PHASE_COUNT];

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
		for (int pi = 0; pi < (int)ST_FM_MAP_PHASE_COUNT; ++pi) {
			s_disp_map_phase_avg[pi] = s_period_sum_map_phase[pi] / s_period_frames;
		}
		Map.Redraw_Tab();
	}

	s_period_sum_frame = 0;
	s_period_sum_render = 0;
	s_period_sum_logic = 0;
	s_period_sum_theme = 0;
	for (int pi = 0; pi < (int)ST_FM_MAP_PHASE_COUNT; ++pi) {
		s_period_sum_map_phase[pi] = 0;
	}
	s_period_frames = 0;
	s_period_hz_start = hz_now;
}

void StFrameMeter_FrameBegin(void)
{
	s_frame_hz0 = St_FrameMeter_ReadHz200();

	s_render_sum_this_frame = 0;
	s_theme_sum_this_frame = 0;
	s_logic_sum_this_frame = 0;
	for (int pi = 0; pi < (int)ST_FM_MAP_PHASE_COUNT; ++pi) {
		s_map_phase_sum_this_frame[pi] = 0;
	}
}

void StFrameMeter_FrameEnd(void)
{
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	unsigned long frame_dt = hz1 - s_frame_hz0;

	s_period_sum_frame += frame_dt;
	s_period_sum_render += s_render_sum_this_frame;
	s_period_sum_logic += s_logic_sum_this_frame;
	s_period_sum_theme += s_theme_sum_this_frame;
	for (int pi = 0; pi < (int)ST_FM_MAP_PHASE_COUNT; ++pi) {
		s_period_sum_map_phase[pi] += s_map_phase_sum_this_frame[pi];
	}
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

void StFrameMeter_MapPhaseBegin(StFrameMeterMapPhase phase)
{
	unsigned int ix = (unsigned int)phase;
	if (ix >= (unsigned int)ST_FM_MAP_PHASE_COUNT) {
		return;
	}
	if (s_map_phase_depth[ix]++ == 0) {
		s_map_phase_hz0[ix] = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_MapPhaseEnd(StFrameMeterMapPhase phase)
{
	unsigned int ix = (unsigned int)phase;
	if (ix >= (unsigned int)ST_FM_MAP_PHASE_COUNT) {
		return;
	}
	if (s_map_phase_depth[ix] == 0 || --s_map_phase_depth[ix] != 0) {
		return;
	}
	unsigned long hz1 = St_FrameMeter_ReadHz200();
	s_map_phase_sum_this_frame[ix] += hz1 - s_map_phase_hz0[ix];
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
	static const unsigned char k_map_phase_colours[ST_FM_MAP_PHASE_COUNT] = {
		(unsigned char)CYAN,
		(unsigned char)GREEN,
		(unsigned char)LTGREEN,
		(unsigned char)BLUE,
		(unsigned char)RED,
		(unsigned char)PURPLE,
	};

	for (int row = 0; row < 4; ++row) {
		if (row == 1) {
			/*
			 * Graphics (render): LTGREY = total render. Overlays inside that span (_hz200 scale,
			 * left → right): prep/scroll bookkeeping, terrain icons/smudges/overlays,
			 * Layer GROUND/AIR/TOP draws, fog/shroud shadow rects.
			 */
			long span_r = ((long)(s_disp_render_avg * (unsigned long)bw) / scale);
			if (span_r < 0) {
				continue;
			}
			int ir = (int)span_r;
			if (ir > bw) {
				ir = bw;
			}
			if (ir > 0) {
				page->Fill_Rect(x0, row, x0 + ir - 1, row, LTGREY);
			}
			int x = x0;
			int remain = ir;
			for (int pi = 0; pi < (int)ST_FM_MAP_PHASE_COUNT && remain > 0; ++pi) {
				long sw = ((long)(s_disp_map_phase_avg[pi] * (unsigned long)bw) / scale);
				if (sw < 0) {
					sw = 0;
				}
				int w = (int)sw;
				if (w > remain) {
					w = remain;
				}
				if (w > 0) {
					page->Fill_Rect(x, row, x + w - 1, row, k_map_phase_colours[pi]);
					x += w;
					remain -= w;
				}
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
