/*
 * ST frame profiler: rolling ~500 ms (_hz200) averages, tab-strip bar display.
 */
#include "function.h"
#include "ATARILIB/st_frame_meter.h"

#ifdef ST_FRAME_BAR_PROFILE

#include "gbuffer.h"
#include "wwstd.h"
#include <stdint.h>

#ifndef ST_FRAME_BAR_HZ200_TICKS_ONE_SECOND
#define ST_FRAME_BAR_HZ200_TICKS_ONE_SECOND 200
#endif

#ifndef ST_FRAME_BAR_HZ200_TICKS_PERIOD
#define ST_FRAME_BAR_HZ200_TICKS_PERIOD 100 /* 500 ms at 200 Hz (~2 updates/sec) */
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
static unsigned long s_period_sum_map_phase[ST_FM_MAP_PHASE_COUNT];
static unsigned long s_period_frames;

static unsigned long s_disp_frame_avg;
static unsigned long s_disp_render_avg;
static unsigned long s_disp_logic_avg;
static unsigned long s_disp_theme_avg;
static unsigned long s_disp_c2p_avg;
static unsigned long s_disp_blit_avg;
static unsigned long s_disp_map_phase_avg[ST_FM_MAP_PHASE_COUNT];
static unsigned long s_disp_fps_x100;
bool StFrameMeterPendingRedraw;

enum {
	ST_FRAME_BAR_FPS_CHAR_W = 8,
	ST_FRAME_BAR_FPS_CHAR_H = 8,
	ST_FRAME_BAR_FPS_MAX_CHARS = 7, /* optional clipped prefix 'c' + "999.99" */
	ST_FRAME_BAR_FPS_PLANE = 0
};

#define ST_FM_GLYPH8(r0, r1, r2, r3, r4, r5, r6, r7) \
	{ \
		{ \
			(uint16_t)((uint16_t)(r0) << 8), (uint16_t)((uint16_t)(r1) << 8), \
			(uint16_t)((uint16_t)(r2) << 8), (uint16_t)((uint16_t)(r3) << 8), \
			(uint16_t)((uint16_t)(r4) << 8), (uint16_t)((uint16_t)(r5) << 8), \
			(uint16_t)((uint16_t)(r6) << 8), (uint16_t)((uint16_t)(r7) << 8) \
		}, \
		{ \
			(uint16_t)(r0), (uint16_t)(r1), (uint16_t)(r2), (uint16_t)(r3), \
			(uint16_t)(r4), (uint16_t)(r5), (uint16_t)(r6), (uint16_t)(r7) \
		} \
	}

enum StFrameMeterGlyph {
	ST_FM_GLYPH_0 = 0,
	ST_FM_GLYPH_1,
	ST_FM_GLYPH_2,
	ST_FM_GLYPH_3,
	ST_FM_GLYPH_4,
	ST_FM_GLYPH_5,
	ST_FM_GLYPH_6,
	ST_FM_GLYPH_7,
	ST_FM_GLYPH_8,
	ST_FM_GLYPH_9,
	ST_FM_GLYPH_DOT,
	ST_FM_GLYPH_C,
	ST_FM_GLYPH_SPACE
};

static const uint16_t k_st_frame_meter_glyphs[][2][ST_FRAME_BAR_FPS_CHAR_H] = {
	ST_FM_GLYPH8(0x1E, 0x33, 0x63, 0x6B, 0x73, 0x66, 0x3C, 0x00),
	ST_FM_GLYPH8(0x0C, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00),
	ST_FM_GLYPH8(0x3E, 0x63, 0x03, 0x0E, 0x38, 0x60, 0x7F, 0x00),
	ST_FM_GLYPH8(0x3E, 0x63, 0x03, 0x1E, 0x03, 0x63, 0x3E, 0x00),
	ST_FM_GLYPH8(0x06, 0x0E, 0x1E, 0x36, 0x66, 0x7F, 0x06, 0x00),
	ST_FM_GLYPH8(0x7F, 0x60, 0x7E, 0x03, 0x03, 0x63, 0x3E, 0x00),
	ST_FM_GLYPH8(0x1E, 0x30, 0x60, 0x7E, 0x63, 0x63, 0x3E, 0x00),
	ST_FM_GLYPH8(0x7F, 0x63, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x00),
	ST_FM_GLYPH8(0x3E, 0x63, 0x63, 0x3E, 0x63, 0x63, 0x3E, 0x00),
	ST_FM_GLYPH8(0x3E, 0x63, 0x63, 0x3F, 0x03, 0x06, 0x3C, 0x00),
	ST_FM_GLYPH8(0x00, 0x00, 0x00, 0x00, 0x18, 0x3C, 0x18, 0x00),
	ST_FM_GLYPH8(0x1C, 0x36, 0x30, 0x30, 0x30, 0x36, 0x1C, 0x00),
	ST_FM_GLYPH8(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)
};

static int St_FrameMeter_GlyphIndex(char c)
{
	if (c >= '0' && c <= '9') {
		return (int)(ST_FM_GLYPH_0 + (c - '0'));
	}

	switch (c) {
	case '.':
		return (int)ST_FM_GLYPH_DOT;
	case 'c':
		return (int)ST_FM_GLYPH_C;
	default:
		break;
	}

	return (int)ST_FM_GLYPH_SPACE;
}

static int St_FrameMeter_FormatFPS(char *text, unsigned long fps_x100)
{
	char digits[3];
	int digit_count = 0;
	int len = 0;
	unsigned long whole;
	unsigned long frac;

	if (text == nullptr) {
		return 0;
	}

	if (fps_x100 > 99999UL) {
		fps_x100 = 99999UL;
	}

	whole = fps_x100 / 100UL;
	frac = fps_x100 % 100UL;

	do {
		digits[digit_count++] = (char)('0' + (whole % 10UL));
		whole /= 10UL;
	} while (whole != 0 && digit_count < (int)sizeof(digits));

	while (digit_count > 0) {
		text[len++] = digits[--digit_count];
	}

	text[len++] = '.';
	text[len++] = (char)('0' + ((frac / 10UL) % 10UL));
	text[len++] = (char)('0' + (frac % 10UL));
	text[len] = '\0';
	return len;
}

static void St_FrameMeter_ClearFPSArea(GraphicViewPortClass *page, int x0, int bw)
{
	if (page == nullptr) {
		return;
	}

	int box_w = ST_FRAME_BAR_FPS_MAX_CHARS * ST_FRAME_BAR_FPS_CHAR_W;
	int box_x = x0 + bw - box_w;
	int box_x2 = box_x + box_w - 1;
	int box_y2 = ST_FRAME_BAR_FPS_CHAR_H - 1;
	if (box_x < 0) {
		box_x = 0;
	}
	if (box_x2 >= page->Get_Width()) {
		box_x2 = page->Get_Width() - 1;
	}
	if (box_y2 >= page->Get_Height()) {
		box_y2 = page->Get_Height() - 1;
	}
	if (box_x <= box_x2 && box_y2 >= 0) {
		page->Fill_Rect(box_x, 0, box_x2, box_y2, BLACK);
	}
}

static void St_FrameMeter_XorFPSText(
	GraphicViewPortClass *page,
	int text_x,
	const char *text,
	int len)
{
	if (page == nullptr || text == nullptr || len <= 0) {
		return;
	}

	GraphicBufferClass *gb = page->Get_Graphic_Buffer();
	if (gb == nullptr || !gb->Uses_ST_LoRes_Planar_Layout()) {
		return;
	}

	uint16_t *planar_root = (uint16_t *)(void *)page->Get_Offset();
	if (planar_root == nullptr) {
		return;
	}

	int row_bytes = page->Get_Pitch();
	if (row_bytes <= 0) {
		row_bytes = gb->Get_Width() / 2;
	}
	if (row_bytes <= 0) {
		return;
	}

	int rows = page->Get_Height();
	if (rows > ST_FRAME_BAR_FPS_CHAR_H) {
		rows = ST_FRAME_BAR_FPS_CHAR_H;
	}

	int row_words = row_bytes >> 1;
	int abs_y = page->Get_YPos();

	for (int ci = 0; ci < len; ++ci) {
		int glyph = St_FrameMeter_GlyphIndex(text[ci]);
		int abs_x = page->Get_XPos() + text_x + (ci * ST_FRAME_BAR_FPS_CHAR_W);
		int phase = ((abs_x & 8) != 0) ? 1 : 0;
		int word_off = (abs_x >> 4) * 4 + ST_FRAME_BAR_FPS_PLANE;

		for (int row = 0; row < rows; ++row) {
			uint16_t mask = k_st_frame_meter_glyphs[glyph][phase][row];
			if (mask == 0) {
				continue;
			}
			uint16_t *dst = planar_root + (size_t)(abs_y + row) * (size_t)row_words + (size_t)word_off;
			*dst ^= mask;
		}
	}
}

#undef ST_FM_GLYPH8

static void St_FrameMeter_DrawFPS(GraphicViewPortClass *page, int x0, int bw)
{
	char text[ST_FRAME_BAR_FPS_MAX_CHARS + 1];
	int prefix = Debug_Coalesced_Clipped_Redraw ? 1 : 0;
	if (prefix) {
		text[0] = 'c';
	}
	int len = St_FrameMeter_FormatFPS(text + prefix, s_disp_fps_x100) + prefix;
	int text_x = x0 + bw - (len * ST_FRAME_BAR_FPS_CHAR_W);
	St_FrameMeter_XorFPSText(page, text_x, text, len);
}

static void St_FrameMeter_MaybeRollPeriod(unsigned long hz_now)
{
	if (s_period_hz_start == 0) {
		s_period_hz_start = hz_now;
		return;
	}

	unsigned long elapsed = hz_now - s_period_hz_start;
	if (elapsed < (unsigned long)ST_FRAME_BAR_HZ200_TICKS_PERIOD) {
		return;
	}

	if (s_period_frames != 0) {
		s_disp_frame_avg = s_period_sum_frame / s_period_frames;
		s_disp_render_avg = s_period_sum_render / s_period_frames;
		s_disp_logic_avg = s_period_sum_logic / s_period_frames;
		s_disp_theme_avg = s_period_sum_theme / s_period_frames;
		s_disp_c2p_avg = s_period_sum_c2p / s_period_frames;
		s_disp_blit_avg = s_period_sum_blit / s_period_frames;
		s_disp_fps_x100 =
			((s_period_frames * (unsigned long)ST_FRAME_BAR_HZ200_TICKS_ONE_SECOND * 100UL) + (elapsed / 2)) / elapsed;
		if (s_disp_fps_x100 > 99999UL) {
			s_disp_fps_x100 = 99999UL;
		}
		for (int pi = 0; pi < (int)ST_FM_MAP_PHASE_COUNT; ++pi) {
			s_disp_map_phase_avg[pi] = s_period_sum_map_phase[pi] / s_period_frames;
		}
		StFrameMeterPendingRedraw = true;
	}

	s_period_sum_frame = 0;
	s_period_sum_render = 0;
	s_period_sum_logic = 0;
	s_period_sum_theme = 0;
	s_period_sum_c2p = 0;
	s_period_sum_blit = 0;
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
	s_c2p_sum_this_frame = 0;
	s_blit_sum_this_frame = 0;
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
	s_period_sum_c2p += s_c2p_sum_this_frame;
	s_period_sum_blit += s_blit_sum_this_frame;
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

void StFrameMeter_C2PBegin(void)
{
	if (s_c2p_depth++ == 0) {
		s_c2p_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_C2PEnd(void)
{
	if (s_c2p_depth == 0 || --s_c2p_depth != 0) {
		return;
	}
	s_c2p_sum_this_frame += St_FrameMeter_ReadHz200() - s_c2p_hz0;
}

void StFrameMeter_BlitBegin(void)
{
	if (s_blit_depth++ == 0) {
		s_blit_hz0 = St_FrameMeter_ReadHz200();
	}
}

void StFrameMeter_BlitEnd(void)
{
	if (s_blit_depth == 0 || --s_blit_depth != 0) {
		return;
	}
	s_blit_sum_this_frame += St_FrameMeter_ReadHz200() - s_blit_hz0;
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
	page->Fill_Rect(x0, 0, x0 + bw - 1, 3, BLACK);
	St_FrameMeter_ClearFPSArea(page, x0, bw);

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

	St_FrameMeter_DrawFPS(page, x0, bw);
}

#endif /* ST_FRAME_BAR_PROFILE */
