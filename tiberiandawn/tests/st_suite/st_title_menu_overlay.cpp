/*
 * Interactive: TITLE.CPS via Load_Title_Screen, then paint the main-menu style overlay
 * (green dialog panel + 6pt gradient button labels) on the same buffer as Main_Menu's
 * first redraw — without linking MENUS.CPP / gadget stack.
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

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Load_Title_Screen(char *name, GraphicViewPortClass *video_page, unsigned char *palette);
extern void *Load_Alloc_Data(FileClass &file);

#define ST_HW_PAL_COUNT 16
#define ST_TITLE_MIX_NAME "CONQUER.MIX"
#define ST_TITLE_CPS_NAME "TITLE.CPS"
#define ST_GRAD_FONT_NAME "GRAD6FNT.FNT"

static void st_fill_bytes(volatile unsigned char *dst, unsigned char value, size_t count)
{
	if (!dst)
		return;
	for (size_t i = 0; i < count; i++)
		dst[i] = value;
}

/* Same table as DIALOG.CPP Simple_Text_Print::_textfontpal (gradient remap rows). */
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

/* DIALOG.CPP Simple_Text_Print::_textpalmedium (TPF_MEDIUM_COLOR for gradient buttons). */
static const unsigned char k_textpalmedium[16] = {
	0, 25, 119, 41, 0, 158, 0, 178, 125, 0, 202, 0, 0, 0, 0, 0};

/* Main_Menu (non-NEWMENU) layout from MENUS.CPP */
enum {
	D_DIALOG_X = 85,
	D_DIALOG_Y = 0,
	D_DIALOG_W = 152,
	D_DIALOG_H = 136 * 2,
	D_START_X = 98,
	D_START_Y = 35 * 2,
	D_START_W = 125,
	D_START_H = 9 * 2,
	D_LOAD_Y = 53 * 2,
	D_MULTI_Y = 71 * 2,
	D_INTRO_Y = 89 * 2,
	D_EXIT_X = 128,
	D_EXIT_Y = 111 * 2,
	D_EXIT_W = 63,
	D_EXIT_H = 9 * 2,
};

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

/*
 * TextButtonClass::Draw_Background for TPF_6PT_GRAD: BOXSTYLE_GREEN_RAISED via Draw_Box.
 * Flat CC_GREEN_BKGD fill + same bevel as DIALOG.CPP (BTEXTURE.SHP is not in retail TD).
 */
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

/*
 * Match Fancy_Text_Print idle main-menu text: TPF_6PT_GRAD | TPF_USE_GRAD_PAL |
 * TPF_MEDIUM_COLOR | TPF_NOSHADOW | TPF_CENTER (centering done in st_draw_grad_button).
 */
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

	const void *font = grad6;
	int xspace = 1;
	int yspace = 0;
	xspace -= 1; /* TPF_6PT_GRAD */
	fontpalette[2] = (unsigned char)back;
	fontpalette[3] = (unsigned char)back;
	xspace -= 1; /* TPF_NOSHADOW */
	yspace -= 2;
	fontpalette[0] = (unsigned char)back;
	fontpalette[1] = (unsigned char)fore;

	FontXSpacing = xspace;
	FontYSpacing = yspace;
	Set_Font(font);
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

int st_run_interactive_title_menu_overlay(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char pal[768];
	void *grad_font = NULL;
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);
	Setscreen(-1L, -1L, 0);

	unsigned char *grad_raw = NULL;
	size_t grad_len = 0;
	int gmx = st_mix_extract_file("LOCAL.MIX", ST_GRAD_FONT_NAME, &grad_raw, &grad_len);
	if (gmx != 0 || !grad_raw) {
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("SKIP: LOCAL.MIX %s err=%d\n", ST_GRAD_FONT_NAME, gmx);
		return 0;
	}
	FILE *gf = fopen(ST_GRAD_FONT_NAME, "wb");
	if (!gf || fwrite(grad_raw, 1, grad_len, gf) != grad_len) {
		free(grad_raw);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		if (gf)
			fclose(gf);
		printf("SKIP: cannot write %s\n", ST_GRAD_FONT_NAME);
		return 0;
	}
	fclose(gf);
	free(grad_raw);
	grad_raw = NULL;

	{
		CCFileClass gfile(ST_GRAD_FONT_NAME);
		grad_font = Load_Alloc_Data(gfile);
	}
	remove(ST_GRAD_FONT_NAME);
	if (!grad_font) {
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("SKIP: Load_Alloc_Data failed for font\n");
		return 0;
	}
	unsigned char *tos_screen = (unsigned char *)Logbase();
	GraphicBufferClass screen;
	screen.Init(320, 200, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);
	/*
	 * Point the ST shifter at our draw buffer before Load_Title_Screen and UI draws.
	 * Otherwise Physbase still references the previous framebuffer: you see a shifted /
	 * wrong image until the final Setscreen() "snaps" the display to this buffer.
	 */
	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

	st_fill_bytes((volatile unsigned char *)CurrentPalette, 0x01, 768);
	{
		unsigned char warm[768];
		memcpy(warm, kStTemperatPal768, 768);
		Set_Palette(warm);
	}
	vp.Clear(0);

	Load_Title_Screen((char *)ST_TITLE_CPS_NAME, &vp, pal);

	Set_Palette(pal);
	St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal);

	Set_Logic_Page(&vp);
	st_draw_dialog_green_border(vp);

	st_paint_menu_button(vp, grad_font, "Start New Game", D_START_X, D_START_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Load Mission", D_START_X, D_LOAD_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Multiplayer Game", D_START_X, D_MULTI_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Intro", D_START_X, D_INTRO_Y, D_START_W, D_START_H);
	st_paint_menu_button(vp, grad_font, "Exit Game", D_EXIT_X, D_EXIT_Y, D_EXIT_W, D_EXIT_H);

	Vsync();

	int ok = st_read_yes_no();

	delete[] (char *)grad_font;
	grad_font = NULL;

	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return ok ? 0 : 1;
}
