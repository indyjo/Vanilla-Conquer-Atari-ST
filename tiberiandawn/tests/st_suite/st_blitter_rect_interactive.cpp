/*
 * Interactive: blit a rectangle from off-screen TITLE.CPS onto a cached
 * checkerboard screen. Tune sx/sy/dx/dy/w/h with cursor keys.
 *
 *   Left/Right  select parameter (horizontal bar)
 *   Up/Down     decrease / increase selected value
 *   Esc/Q       exit
 */

#include "st_blitter_rect_interactive.h"

#include "c2p.h"
#include "function.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_audio_asset_autotest.h"
#include "st_blit.h"
#include "st_temperat_palette.h"
#include "st_test_linkage.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16
#define ST_TITLE_CPS_NAME "TITLE.CPS"
#define ST_PLANAR_FRAME_BYTES (ST_PLANAR_BYTES_PER_LINE * ST_PLANAR_HEIGHT)
#define ST_PARAM_COUNT 6
#define ST_UI_BAR_Y 192
#define ST_UI_BAR_H 8

enum {
	ST_SCAN_UP = 0x48,
	ST_SCAN_DOWN = 0x50,
	ST_SCAN_LEFT = 0x4B,
	ST_SCAN_RIGHT = 0x4D
};

enum {
	ST_COL_OUTLINE_DIM = 1,
	ST_COL_TAB_BASE = 8,
	ST_COL_SEL = ST_COL_TAB_BASE + 2, /* yellow; third tab from left */
	ST_VT52_FG_DEFAULT = 15,
	ST_VT52_BG_DEFAULT = 0,
	ST_VT52_STATUS_ROW = 24
};

/* GEMDOS VT52 (conout): ESC b / ESC c set fg/bg; low 4 bits of next char = palette index. */
static char st_vt52_color_char(unsigned char pal_idx)
{
	return (char)(32 + (pal_idx & 15u));
}

static void st_vt52_set_colors(unsigned char fg, unsigned char bg)
{
	printf("\033b%c\033c%c", st_vt52_color_char(fg), st_vt52_color_char(bg));
}

static void st_vt52_goto(int row_1based, int col_1based)
{
	printf("\033Y%c%c", (char)(row_1based + 32), (char)(col_1based + 32));
}

static const char k_param_vt52_label[ST_PARAM_COUNT] = {
	'x', 'y', 'X', 'Y', 'W', 'H'
};

typedef struct {
	int sx;
	int sy;
	int dx;
	int dy;
	int w;
	int h;
} StBlitRectParams;

static void st_hw_palette_read(unsigned short *dst16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++) {
		dst16[i] = pr[i];
	}
}

static void st_hw_palette_write(const unsigned short *src16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++) {
		pr[i] = src16[i];
	}
}

static void st_planar_hline(uint8_t *planar, int x0, int x1, int y, unsigned char color)
{
	if (y < 0 || y >= ST_PLANAR_HEIGHT) {
		return;
	}
	if (x0 > x1) {
		const int t = x0;
		x0 = x1;
		x1 = t;
	}
	if (x0 < 0) {
		x0 = 0;
	}
	if (x1 >= ST_PLANAR_WIDTH) {
		x1 = ST_PLANAR_WIDTH - 1;
	}
	for (int x = x0; x <= x1; x++) {
		ST_Planar_PutPixel(
			planar,
			ST_PLANAR_BYTES_PER_LINE,
			ST_PLANAR_WIDTH,
			ST_PLANAR_HEIGHT,
			x,
			y,
			color);
	}
}

static void st_planar_vline(uint8_t *planar, int x, int y0, int y1, unsigned char color)
{
	if (x < 0 || x >= ST_PLANAR_WIDTH) {
		return;
	}
	if (y0 > y1) {
		const int t = y0;
		y0 = y1;
		y1 = t;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (y1 >= ST_PLANAR_HEIGHT) {
		y1 = ST_PLANAR_HEIGHT - 1;
	}
	for (int y = y0; y <= y1; y++) {
		ST_Planar_PutPixel(
			planar,
			ST_PLANAR_BYTES_PER_LINE,
			ST_PLANAR_WIDTH,
			ST_PLANAR_HEIGHT,
			x,
			y,
			color);
	}
}

static void st_build_checker_planar(uint8_t *planar, unsigned char color_a, unsigned char color_b)
{
	for (int y = 0; y < ST_PLANAR_HEIGHT; y++) {
		for (int x = 0; x < ST_PLANAR_WIDTH; x++) {
			const unsigned char c = (((x >> 4) ^ (y >> 4)) & 1) ? color_a : color_b;
			ST_Planar_PutPixel(
				planar,
				ST_PLANAR_BYTES_PER_LINE,
				ST_PLANAR_WIDTH,
				ST_PLANAR_HEIGHT,
				x,
				y,
				c);
		}
	}
}

static int *st_param_ptr(StBlitRectParams *p, int index)
{
	switch (index) {
	case 0:
		return &p->sx;
	case 1:
		return &p->sy;
	case 2:
		return &p->dx;
	case 3:
		return &p->dy;
	case 4:
		return &p->w;
	case 5:
		return &p->h;
	default:
		return &p->sx;
	}
}

static void st_blit_params_clamp(StBlitRectParams *p)
{
	if (p->w < 1) {
		p->w = 1;
	}
	if (p->h < 1) {
		p->h = 1;
	}
	if (p->w > ST_PLANAR_WIDTH) {
		p->w = ST_PLANAR_WIDTH;
	}
	if (p->h > ST_PLANAR_HEIGHT) {
		p->h = ST_PLANAR_HEIGHT;
	}
	if (p->sx < 0) {
		p->sx = 0;
	}
	if (p->sy < 0) {
		p->sy = 0;
	}
	if (p->dx < 0) {
		p->dx = 0;
	}
	if (p->dy < 0) {
		p->dy = 0;
	}
	if (p->sx + p->w > ST_PLANAR_WIDTH) {
		p->sx = ST_PLANAR_WIDTH - p->w;
	}
	if (p->sy + p->h > ST_PLANAR_HEIGHT) {
		p->sy = ST_PLANAR_HEIGHT - p->h;
	}
	if (p->dx + p->w > ST_PLANAR_WIDTH) {
		p->dx = ST_PLANAR_WIDTH - p->w;
	}
	if (p->dy + p->h > ST_PLANAR_HEIGHT) {
		p->dy = ST_PLANAR_HEIGHT - p->h;
	}
}

static void st_print_params(const StBlitRectParams *p, int selected, int blit_ok)
{
	const int values[ST_PARAM_COUNT] = {
		p->sx, p->sy, p->dx, p->dy, p->w, p->h
	};

	st_vt52_goto(ST_VT52_STATUS_ROW, 1);
	st_vt52_set_colors(ST_VT52_FG_DEFAULT, ST_VT52_BG_DEFAULT);
	for (int i = 0; i < ST_PARAM_COUNT; i++) {
		if (i == selected) {
			st_vt52_set_colors(ST_COL_SEL, ST_VT52_BG_DEFAULT);
		} else {
			st_vt52_set_colors(ST_VT52_FG_DEFAULT, ST_VT52_BG_DEFAULT);
		}
		printf("%c%3d", k_param_vt52_label[i], values[i]);
		if (i + 1 < ST_PARAM_COUNT) {
			printf(" ");
		}
	}
	st_vt52_set_colors(ST_VT52_FG_DEFAULT, ST_VT52_BG_DEFAULT);
	printf(" %s", blit_ok ? "OK" : "FAIL");
	fflush(stdout);
}

static void st_draw_rect_outline(
	uint8_t *planar,
	int x,
	int y,
	int w,
	int h,
	unsigned char color)
{
	if (w <= 0 || h <= 0) {
		return;
	}
	const int x1 = x + w - 1;
	const int y1 = y + h - 1;
	st_planar_hline(planar, x, x1, y, color);
	st_planar_hline(planar, x, x1, y1, color);
	st_planar_vline(planar, x, y, y1, color);
	st_planar_vline(planar, x1, y, y1, color);
}

static void st_draw_ui_overlay(uint8_t *screen, const StBlitRectParams *p, int selected)
{
	const int seg_w = ST_PLANAR_WIDTH / ST_PARAM_COUNT;

	/* Horizontal parameter bar: selected segment yellow, others dim. */
	for (int i = 0; i < ST_PARAM_COUNT; i++) {
		const int x0 = i * seg_w;
		const int x1 = (i == ST_PARAM_COUNT - 1) ? ST_PLANAR_WIDTH : x0 + seg_w;
		const unsigned char c = (i == selected) ? ST_COL_SEL : ST_COL_OUTLINE_DIM;
		for (int y = ST_UI_BAR_Y; y < ST_UI_BAR_Y + ST_UI_BAR_H; y++) {
			st_planar_hline(screen, x0, x1 - 1, y, c);
		}
	}

	/* Destination rect: dim outline, bright edge for the active spatial parameter. */
	st_draw_rect_outline(screen, p->dx, p->dy, p->w, p->h, ST_COL_OUTLINE_DIM);
	const int dx1 = p->dx + p->w - 1;
	const int dy1 = p->dy + p->h - 1;
	switch (selected) {
	case 2:
		st_planar_vline(screen, p->dx, p->dy, dy1, ST_COL_SEL);
		break;
	case 3:
		st_planar_hline(screen, p->dx, dx1, p->dy, ST_COL_SEL);
		break;
	case 4:
		st_planar_vline(screen, dx1, p->dy, dy1, ST_COL_SEL);
		break;
	case 5:
		st_planar_hline(screen, p->dx, dx1, dy1, ST_COL_SEL);
		break;
	default:
		break;
	}

	if (selected == 0 && p->sx > 0) {
		st_planar_hline(screen, 0, p->sx - 1, 0, ST_COL_SEL);
	}
	if (selected == 1 && p->sy > 0) {
		st_planar_vline(screen, 0, 0, p->sy - 1, ST_COL_SEL);
	}
}

static BOOL st_redraw_blit(
	uint8_t *screen,
	const uint8_t *checker_bg,
	const uint8_t *src,
	const StBlitRectParams *p,
	int selected)
{
	Vsync();
	memcpy(screen, checker_bg, (size_t)ST_PLANAR_FRAME_BYTES);
	const BOOL ok = ST_Blit_Planar_Rect_Blit(
		src,
		ST_PLANAR_BYTES_PER_LINE,
		p->sx,
		p->sy,
		screen,
		ST_PLANAR_BYTES_PER_LINE,
		p->dx,
		p->dy,
		p->w,
		p->h);
	st_draw_ui_overlay(screen, p, selected);
	Setscreen((long)screen, (long)screen, -1L);
	return ok;
}

static BOOL st_load_title_offscreen(uint8_t *offscreen, unsigned char *pal)
{
	GraphicBufferClass off_buf;
	off_buf.Init(ST_PLANAR_WIDTH, ST_PLANAR_HEIGHT, offscreen, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass off_vp(&off_buf, 0, 0, ST_PLANAR_WIDTH, ST_PLANAR_HEIGHT);
	off_vp.Clear(0);
	memset(CurrentPalette, 0x01, 768);
	Load_Title_Screen((char *)ST_TITLE_CPS_NAME, &off_vp, pal);
	return TRUE;
}

static int st_decode_key(long w, int *out_scan, int *out_ch)
{
	*out_scan = (int)((w >> 16) & 0xFF);
	*out_ch = (int)(w & 0xFF);
	return 0;
}

int st_run_interactive_blitter_rect(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char pal[768];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);
	st_conterm_keyclick_mute_push();

	uint8_t *offscreen = (uint8_t *)malloc(ST_PLANAR_FRAME_BYTES);
	uint8_t *checker_bg = (uint8_t *)malloc(ST_PLANAR_FRAME_BYTES);
	if (!offscreen || !checker_bg) {
		free(offscreen);
		free(checker_bg);
		st_conterm_keyclick_mute_pop();
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("\nFAIL: allocation\n");
		return 1;
	}

	Setscreen(-1L, -1L, 0);
	uint8_t *screen = (uint8_t *)Logbase();
	if (!screen) {
		free(offscreen);
		free(checker_bg);
		st_conterm_keyclick_mute_pop();
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("\nFAIL: no Logbase\n");
		return 1;
	}

	memset(CurrentPalette, 0x01, 768);
	{
		unsigned char warm[768];
		memcpy(warm, kStTemperatPal768, sizeof(warm));
		Set_Palette(warm);
	}
	if (!st_load_title_offscreen(offscreen, pal)) {
		free(offscreen);
		free(checker_bg);
		st_conterm_keyclick_mute_pop();
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("\nFAIL: TITLE.CPS load\n");
		return 1;
	}
	Set_Palette(pal);
	st_build_checker_planar(checker_bg, 2, 4);

	StBlitRectParams params;
	params.sx = 0;
	params.sy = 0;
	params.dx = 80;
	params.dy = 60;
	params.w = 160;
	params.h = 120;
	st_blit_params_clamp(&params);

	int selected = 0;

	printf("\n");
	printf("Interactive blit rect (TITLE.CPS -> checkerboard)\n");
	printf("Left/Right: select  Up/Down: adjust  Esc/Q: exit\n");
	printf("Status line: x/y=src  X/Y=dst  W/H=size (selected = yellow)\n");

	for (;;) {
		const BOOL blit_ok = st_redraw_blit(screen, checker_bg, offscreen, &params, selected);
		st_print_params(&params, selected, blit_ok);

		const long w = Crawcin();
		int scan = 0;
		int ch = 0;
		st_decode_key(w, &scan, &ch);

		if (ch == 27 || ch == 'q' || ch == 'Q') {
			break;
		}

		if (scan == ST_SCAN_LEFT) {
			selected = (selected + ST_PARAM_COUNT - 1) % ST_PARAM_COUNT;
			continue;
		}
		if (scan == ST_SCAN_RIGHT) {
			selected = (selected + 1) % ST_PARAM_COUNT;
			continue;
		}

		int delta = 0;
		if (scan == ST_SCAN_UP) {
			delta = 1;
		} else if (scan == ST_SCAN_DOWN) {
			delta = -1;
		} else if (ch == '+' || ch == '=') {
			delta = 1;
		} else if (ch == '-' || ch == '_') {
			delta = -1;
		} else {
			continue;
		}

		int *value = st_param_ptr(&params, selected);
		*value += delta;
		st_blit_params_clamp(&params);
	}

	printf("\nDone.\n");

	st_conterm_keyclick_mute_pop();
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	free(offscreen);
	free(checker_bg);
	return 0;
}
