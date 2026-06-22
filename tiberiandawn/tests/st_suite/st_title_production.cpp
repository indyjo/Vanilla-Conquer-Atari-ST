/*
 * Interactive: TITLE.CPS production path, main-menu overlay, and mouse cursor
 * (former tests 4, 5, 6 combined).
 */

#include "function.h"
#include "c2p.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_mix_minimal.h"
#include "st_text.h"

#include "ccfile.h"
#include "compat.h"
#include "font.h"
#include "mouse.h"
#include "shape.h"

#include <math.h>
#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_test_linkage.h"
extern void *Load_Alloc_Data(FileClass &file);

#define ST_HW_PAL_COUNT 16
#define ST_TITLE_CPS_NAME "TITLE.CPS"
#define ST_GRAD_FONT_NAME "GRAD6FNT.FNT"
#define ST_MOUSE_MIX_NAME "MOUSE.SHP"
#define ST_MOUSE_TEMP_FILE "ST_MOUSE.SHP"
#define ST_MOUSE_DEMO_FRAMES 400

static const unsigned char k_textfontpal[16][16] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27, 27, 26, 25, 24},
	{0, 135, 0, 0, 0, 0, 0, 0, 0, 0, 0, 136, 136, 135, 119, 2},
	{0, 159, 0, 0, 0, 0, 0, 0, 0, 0, 0, 142, 143, 159, 41, 167},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 157, 0, 0, 0, 0, 0, 0, 0, 0, 0, 180, 180, 157, 158, 5},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 179, 0, 0, 0, 0, 0, 0, 0, 0, 0, 180, 180, 179, 178, 176},
	{0, 123, 0, 0, 0, 0, 0, 0, 0, 0, 0, 122, 122, 123, 125, 127},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 203, 0, 0, 0, 0, 0, 0, 0, 0, 0, 204, 204, 203, 202, 201},
	{0, 1, 4, 166, 41, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 203, 0, 0, 0, 0, 0, 0, 0, 0, 0, 204, 204, 203, 202, 201},
	{0, 203, 0, 0, 0, 0, 0, 0, 0, 0, 0, 204, 204, 203, 202, 201},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

static const unsigned char k_textpalmedium[16] = {
	0, 25, 119, 41, 0, 158, 0, 178, 125, 0, 202, 0, 0, 0, 0, 0};

/* Main_Menu @ 320x200 (scale_factor == 1 in MENUS.CPP). */
enum {
	D_DIALOG_X = 85,
	D_DIALOG_Y = 0,
	D_DIALOG_W = 152,
	D_DIALOG_H = 136,
	D_START_X = 98,
	D_START_Y = 35,
	D_START_W = 125,
	D_START_H = 9,
	D_LOAD_Y = 53,
	D_MULTI_Y = 71,
	D_INTRO_Y = 89,
	D_EXIT_X = 128,
	D_EXIT_Y = 111,
	D_EXIT_W = 63,
	D_EXIT_H = 9,
};

static void st_hw_palette_read(unsigned short *dst16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++)
		dst16[i] = pr[i];
}

static void st_hw_palette_write(const unsigned short *src16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++)
		pr[i] = src16[i];
}

static void st_draw_dialog_green_border(GraphicViewPortClass &vp)
{
	int x = D_DIALOG_X;
	int y = D_DIALOG_Y;
	int w = D_DIALOG_W;
	int h = D_DIALOG_H;
	w--;
	h--;
	vp.Fill_Rect(x, y, x + w, y + h, (unsigned char)CC_GREEN_BKGD);
	vp.Draw_Rect(x + 1, y + 1, x + w - 1, y + h - 1, 14);
}

static void st_draw_green_raised_button_frame(GraphicViewPortClass &vp, int x, int y, int w, int h)
{
	int iw = w - 1;
	int ih = h - 1;
	vp.Fill_Rect(x, y, x + iw, y + ih, (unsigned char)CC_GREEN_BKGD);
	vp.Draw_Line(x, y + ih, x + iw, y + ih, 12);
	vp.Draw_Line(x + iw, y, x + iw, y + ih, 12);
	vp.Draw_Line(x, y, x + iw, y, 14);
	vp.Draw_Line(x, y, x, y + ih, 14);
	vp.Put_Pixel(x, y + ih, 13);
	vp.Put_Pixel(x + iw, y, 13);
}

static int st_apply_main_menu_grad_font(void const *grad6, bool medium)
{
	unsigned char fontpalette[16];
	const unsigned fore_cc = (unsigned)CC_GREEN;
	unsigned back = (unsigned)TBLACK;
	unsigned fore;

	memset(fontpalette, (int)back, sizeof(fontpalette));
	memcpy(fontpalette, k_textfontpal[fore_cc & 0x0F], 16);
	if (medium) {
		fore = (unsigned)k_textpalmedium[fore_cc & 0x0F];
		memset(&fontpalette[4], (unsigned char)fore, 12);
	} else {
		fore = fontpalette[1];
	}

	int xspace = 0;
	int yspace = -2;
	fontpalette[2] = (unsigned char)back;
	fontpalette[3] = (unsigned char)back;
	fontpalette[0] = (unsigned char)back;
	fontpalette[1] = (unsigned char)fore;

	FontXSpacing = xspace;
	FontYSpacing = yspace;
	Set_Font(grad6);
	Set_Font_Palette(fontpalette);
	return (int)fore;
}

static void st_draw_grad_button(GraphicViewPortClass &vp, char const *label, int bx, int by, int bw, int bh, int fcol)
{
	(void)bh;
	if (!label || !FontPtr)
		return;
	int cx = bx + (bw >> 1) - 1;
	int cy = by + 1;
	int sw = (int)String_Pixel_Width(label);
	int px = cx - (sw >> 1);
	int vpw = vp.Get_Width();
	if (px < 0)
		px = 0;
	if (sw > 0 && px + sw > vpw)
		px = vpw - sw;
	if (px < 0)
		px = 0;
	vp.Print(label, px, cy, fcol, TBLACK);
}

static void st_paint_menu_button(GraphicViewPortClass &vp, void const *grad6, char const *label, int bx, int by, int bw, int bh)
{
	st_draw_green_raised_button_frame(vp, bx, by, bw, bh);
	int fcol = st_apply_main_menu_grad_font(grad6, true);
	st_draw_grad_button(vp, label, bx, by, bw, bh, fcol);
}

static void st_paint_main_menu_overlay(GraphicViewPortClass &vp, void const *grad_font)
{
	st_draw_dialog_green_border(vp);
	st_paint_menu_button(vp, grad_font, "Start New Game", D_START_X, D_START_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Load Mission", D_START_X, D_LOAD_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Multiplayer Game", D_START_X, D_MULTI_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Intro", D_START_X, D_INTRO_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Exit Game", D_EXIT_X, D_EXIT_Y, D_EXIT_W, D_EXIT_H);
}

static void *st_load_mix_asset(const char *mix_name, const char *entry_name, const char *temp_path)
{
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int mx = st_mix_extract_file(mix_name, entry_name, &raw, &raw_len);
	if (mx != 0 || !raw) {
		printf("SKIP: %s %s err=%d\n", mix_name, entry_name, mx);
		return NULL;
	}

	FILE *f = fopen(temp_path, "wb");
	if (!f || fwrite(raw, 1, raw_len, f) != raw_len) {
		free(raw);
		if (f)
			fclose(f);
		printf("SKIP: cannot write %s\n", temp_path);
		return NULL;
	}
	fclose(f);
	free(raw);

	void *data = NULL;
	{
		CCFileClass file(temp_path);
		data = Load_Alloc_Data(file);
	}
	remove(temp_path);
	if (!data) {
		printf("SKIP: Load_Alloc_Data failed for %s\n", entry_name);
	}
	return data;
}

static void st_prime_title_palette(unsigned char *pal)
{
	memset(CurrentPalette, 0x01, 768);
	unsigned char warm[768];
	memcpy(warm, kStTemperatPal768, 768);
	Set_Palette(warm);
	(void)pal;
}

static void st_load_title_screen(GraphicViewPortClass &vp, unsigned char *pal)
{
	Load_Title_Screen((char *)ST_TITLE_CPS_NAME, &vp, pal);
	Set_Palette(pal);
}

int st_run_interactive_title_production_path(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char pal[768];
	void *grad_font = NULL;
	void *mouse_block = NULL;
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	int ok = 0;

	st_hw_palette_read(saved_hw);
	Setscreen(-1L, -1L, 0);

	unsigned char *tos_screen = (unsigned char *)Logbase();
	GraphicBufferClass screen;
	screen.Init(320, 200, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);
	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

	grad_font = st_load_mix_asset("LOCAL.MIX", ST_GRAD_FONT_NAME, ST_GRAD_FONT_NAME);
	if (!grad_font)
		goto restore;

	mouse_block = st_load_mix_asset("LOCAL.MIX", ST_MOUSE_MIX_NAME, ST_MOUSE_TEMP_FILE);
	if (!mouse_block)
		goto restore;

	st_prime_title_palette(pal);
	vp.Clear(0);
	st_load_title_screen(vp, pal);
	Set_Logic_Page(&vp);
	st_paint_main_menu_overlay(vp, grad_font);

	printf("\nTITLE + menu + mouse (animated).\n");
	fflush(stdout);

	{
		WWMouseClass mouse(&vp, 64, 64);
		Set_Mouse_Cursor(0, 0, Extract_Shape(mouse_block, 0));
		Show_Mouse();
		for (int f = 0; f < ST_MOUSE_DEMO_FRAMES; f++) {
			double t = (double)f * (2.0 * M_PI / 280.0);
			int mxp = 160 + (int)(110.0 * sin(t));
			int myp = 100 + (int)(72.0 * sin(t * 1.37 + 0.9));
			DLLForceMouseX = mxp;
			DLLForceMouseY = myp;
			Vsync();
			mouse.Process_Mouse();
		}
		DLLForceMouseX = -1;
		DLLForceMouseY = -1;
	}

	ok = st_read_yes_no();

restore:
	delete[] (char *)grad_font;
	delete[] (char *)mouse_block;
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return ok ? 0 : 1;
}
