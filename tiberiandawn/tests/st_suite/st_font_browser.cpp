/*
 * Interactive: browse .FNT files listed in mix.txt / mix2.txt (union of catalogued fonts).
 *
 * mix.txt:   CCLOCAL.MIX (3POINT..VCR), UPDATE.MIX (12GREEN, 12GRNGRD), UPDATEC.MIX (GRAD12FN)
 * mix2.txt:  DOS local.mix (same .FNT subset as CCLOCAL for several files) + conquer.mix
 *
 * Fonts shared with the DOS catalog are loaded by trying CCLOCAL.MIX, then LOCAL.MIX, then
 * local.mix (8.3 / lowercase as on DOS media).
 */

#include "function.h"
#include "palette.h"
#include "st_font_browser.h"
#include "st_mix_minimal.h"
#include "st_temperat_palette.h"

#include "ccfile.h"
#include "compat.h"
#include "font.h"
#include "gbuffer.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *Load_Alloc_Data(FileClass &file);

#define ST_FB_HW_PAL_COUNT 16
#define ST_FB_SCR_W 320
#define ST_FB_SCR_H 200

/* Same as DIALOG.CPP Simple_Text_Print::_textfontpal / _textpalmedium (gradient fonts). */
static const unsigned char k_fb_textfontpal[16][16] = {
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

static const unsigned char k_fb_textpalmedium[16] = {
	0, 25, 119, 41, 0, 158, 0, 178, 125, 0, 202, 0, 0, 0, 0, 0};

enum StFbFontKind {
	ST_FB_KIND_PLAIN = 0,
	ST_FB_KIND_GRAD,
	ST_FB_KIND_VCR,
	ST_FB_KIND_LED,
	ST_FB_KIND_3PT,
	ST_FB_KIND_8PT,
};

struct StFbEntry {
	char const *const *mix_try;
	char const *filename;
	StFbFontKind kind;
};

static char const *const k_fb_mix_cclocal_then_dos_local[] = {
	"LOCAL.MIX",
	"CCLOCAL.MIX",
	"local.mix",
	NULL,
};

static char const *const k_fb_mix_update_only[] = {
	"UPDATE.MIX",
	NULL,
};

static char const *const k_fb_mix_updatec_only[] = {
	"UPDATEC.MIX",
	NULL,
};

/*
 * Union of *.FNT names appearing in mix.txt (CCLOCAL / UPDATE / UPDATEC) and mix2.txt
 * (local.mix + conquer.mix). Entries that exist on both ST CCLOCAL and DOS local.mix
 * use k_fb_mix_cclocal_then_dos_local.
 */
static const StFbEntry k_fb_fonts[] = {
	{k_fb_mix_cclocal_then_dos_local, "3POINT.FNT", ST_FB_KIND_3PT},
	{k_fb_mix_cclocal_then_dos_local, "6POINT.FNT", ST_FB_KIND_PLAIN},
	{k_fb_mix_cclocal_then_dos_local, "8POINT.FNT", ST_FB_KIND_8PT},
	{k_fb_mix_cclocal_then_dos_local, "HFNT-D.FNT", ST_FB_KIND_PLAIN},
	{k_fb_mix_cclocal_then_dos_local, "FONT6.FNT", ST_FB_KIND_PLAIN},
	{k_fb_mix_cclocal_then_dos_local, "8FAT.FNT", ST_FB_KIND_8PT},
	{k_fb_mix_cclocal_then_dos_local, "GRAD6FNT.FNT", ST_FB_KIND_GRAD},
	{k_fb_mix_cclocal_then_dos_local, "SCOREFNT.FNT", ST_FB_KIND_PLAIN},
	{k_fb_mix_cclocal_then_dos_local, "LED.FNT", ST_FB_KIND_LED},
	{k_fb_mix_cclocal_then_dos_local, "VCR.FNT", ST_FB_KIND_VCR},
	{k_fb_mix_update_only, "12GREEN.FNT", ST_FB_KIND_GRAD},
	{k_fb_mix_update_only, "12GRNGRD.FNT", ST_FB_KIND_GRAD},
	{k_fb_mix_updatec_only, "GRAD12FN.FNT", ST_FB_KIND_GRAD},
};

static void st_fb_hw_read(unsigned short *dst16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_FB_HW_PAL_COUNT; i++)
		dst16[i] = pr[i];
}

static void st_fb_hw_write(const unsigned short *src16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_FB_HW_PAL_COUNT; i++)
		pr[i] = src16[i];
}

static void *st_fb_load_font_from_one_mix(char const *mix, char const *filename)
{
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	if (st_mix_extract_file(mix, filename, &raw, &raw_len) != 0 || !raw)
		return NULL;

	FILE *out = fopen(filename, "wb");
	if (!out || fwrite(raw, 1, raw_len, out) != raw_len) {
		free(raw);
		if (out)
			fclose(out);
		return NULL;
	}
	fclose(out);
	free(raw);

	void *font = NULL;
	{
		CCFileClass file_obj(filename);
		font = Load_Alloc_Data(file_obj);
	}
	remove(filename);
	return font;
}

static void *st_fb_load_font(char const *const *mix_try, char const *filename, char const **out_used_mix)
{
	if (out_used_mix)
		*out_used_mix = NULL;
	if (!mix_try || !filename)
		return NULL;
	for (; *mix_try; ++mix_try) {
		void *f = st_fb_load_font_from_one_mix(*mix_try, filename);
		if (f) {
			if (out_used_mix)
				*out_used_mix = *mix_try;
			return f;
		}
	}
	return NULL;
}

static int st_fb_apply_font(void const *font, StFbFontKind kind)
{
	unsigned char fontpalette[16];
	unsigned const fore_cc = (unsigned)CC_GREEN;
	unsigned const back = (unsigned)TBLACK;

	switch (kind) {
	case ST_FB_KIND_GRAD: {
		unsigned fore;
		memset(fontpalette, (int)back, sizeof(fontpalette));
		memcpy(fontpalette, k_fb_textfontpal[fore_cc & 0x0F], 16);
		fore = (unsigned)k_fb_textpalmedium[fore_cc & 0x0F];
		memset(&fontpalette[4], (unsigned char)fore, 12);
		int xspace = 1;
		int yspace = 0;
		xspace -= 1;
		fontpalette[2] = (unsigned char)back;
		fontpalette[3] = (unsigned char)back;
		xspace -= 1;
		yspace -= 2;
		fontpalette[0] = (unsigned char)back;
		fontpalette[1] = (unsigned char)fore;
		FontXSpacing = xspace;
		FontYSpacing = yspace;
		Set_Font(font);
		Set_Font_Palette(fontpalette);
		return (int)fore;
	}
	case ST_FB_KIND_VCR: {
		memset(fontpalette, (int)back, 16);
		fontpalette[3] = 12;
		fontpalette[9] = 15;
		fontpalette[10] = 200;
		fontpalette[11] = 201;
		fontpalette[12] = 202;
		fontpalette[13] = 203;
		fontpalette[14] = 204;
		fontpalette[15] = 205;
		fontpalette[0] = (unsigned char)back;
		fontpalette[1] = 15;
		FontXSpacing = 0;
		FontYSpacing = -2;
		Set_Font(font);
		Set_Font_Palette(fontpalette);
		return 15;
	}
	case ST_FB_KIND_LED: {
		/* TPF_LED: xspace 1-4; no shadow flag in path used here. */
		memset(fontpalette, (int)back, 16);
		fontpalette[0] = (unsigned char)back;
		fontpalette[1] = 15;
		FontXSpacing = -3;
		FontYSpacing = 0;
		Set_Font(font);
		Set_Font_Palette(fontpalette);
		return 15;
	}
	case ST_FB_KIND_3PT: {
		memset(fontpalette, (int)back, 16);
		fontpalette[0] = (unsigned char)back;
		fontpalette[1] = 15;
		fontpalette[2] = (unsigned char)back;
		fontpalette[3] = (unsigned char)back;
		FontXSpacing = 1;
		FontYSpacing = -2;
		Set_Font(font);
		Set_Font_Palette(fontpalette);
		return 15;
	}
	case ST_FB_KIND_8PT: {
		memset(fontpalette, (int)back, 16);
		fontpalette[0] = (unsigned char)back;
		fontpalette[1] = 15;
		fontpalette[2] = (unsigned char)back;
		fontpalette[3] = (unsigned char)back;
		FontXSpacing = -2;
		FontYSpacing = -6;
		Set_Font(font);
		Set_Font_Palette(fontpalette);
		return 15;
	}
	default: {
		memset(fontpalette, (int)back, 16);
		fontpalette[0] = (unsigned char)back;
		fontpalette[1] = 15;
		fontpalette[2] = (unsigned char)back;
		fontpalette[3] = (unsigned char)back;
		FontXSpacing = 0;
		FontYSpacing = -2;
		Set_Font(font);
		Set_Font_Palette(fontpalette);
		return 15;
	}
	}
}

static void st_fb_status_vt52(int index, int missing, char const *filename, char const *loaded_mix)
{
	int const n = (int)(sizeof(k_fb_fonts) / sizeof(k_fb_fonts[0]));
	printf("\033Y%c%c", (char)(24 + 32), (char)(0 + 32));
	if (missing)
		printf("MISS %s", filename ? filename : "?");
	else
		printf("%2d/%2d %-13s %s", index + 1, n, filename ? filename : "?", loaded_mix ? loaded_mix : "");
	fflush(stdout);
}

static void st_fb_paint(GraphicViewPortClass &vp, void *font, StFbEntry const &e, int missing)
{
	vp.Clear((unsigned char)TBLACK);
	if (missing || !font)
		return;

	int fcol = st_fb_apply_font(font, e.kind);
	char const *lines[] = {
		"AaBbGgZz 0123456789",
		"THE QUICK BROWN FOX",
		"!@#$%%^&*()_+-=[]",
	};
	int y = 8;
	for (size_t li = 0; li < sizeof(lines) / sizeof(lines[0]); li++) {
		vp.Print(lines[li], 8, y, fcol, TBLACK);
		y += (int)FontHeight + 4 + FontYSpacing;
		if (y > ST_FB_SCR_H - 24)
			break;
	}
}

int st_run_interactive_font_browser(void)
{
	unsigned short saved_hw[ST_FB_HW_PAL_COUNT];
	void *font = NULL;
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_fb_hw_read(saved_hw);
	Setscreen(-1L, -1L, 0);

	printf("\nFont browser (catalogued .FNT in mix.txt / mix2.txt).\n");
	printf("CCLOCAL then LOCAL.MIX/local.mix for shared fonts.\n");
	printf("p prev  n/space next  q/ESC quit\n");

	unsigned char *tos_screen = (unsigned char *)Logbase();
	if (!tos_screen) {
		st_fb_hw_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("SKIP: no logbase\n");
		return 1;
	}

	GraphicBufferClass screen;
	screen.Init(ST_FB_SCR_W, ST_FB_SCR_H, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, ST_FB_SCR_W, ST_FB_SCR_H);
	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

	unsigned char pal[768];
	memcpy(pal, kStTemperatPal768, 768);
	Set_Palette(pal);
	St_HW_Palette_Write_Temperat_First16((volatile unsigned short *)0xFF8240L);

	int index = 0;
	int const nfonts = (int)(sizeof(k_fb_fonts) / sizeof(k_fb_fonts[0]));
	char const *loaded_mix = NULL;
	for (;;) {
		if (font) {
			delete[] (char *)font;
			font = NULL;
		}
		StFbEntry const &e = k_fb_fonts[index];
		loaded_mix = NULL;
		font = st_fb_load_font(e.mix_try, e.filename, &loaded_mix);
		int missing = (font == NULL);
		Set_Logic_Page(&vp);
		st_fb_paint(vp, font, e, missing);
		st_fb_status_vt52(index, missing, e.filename, loaded_mix);
		Vsync();
		Vsync();

		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		if (ch == 'q' || ch == 'Q' || ch == 27)
			break;
		if (ch == 'n' || ch == 'N' || ch == ' ' || ch == 13) {
			index++;
			if (index >= nfonts)
				index = 0;
			continue;
		}
		if (ch == 'p' || ch == 'P' || ch == 8) {
			index--;
			if (index < 0)
				index = nfonts - 1;
			continue;
		}
	}

	if (font) {
		delete[] (char *)font;
		font = NULL;
	}

	st_fb_hw_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	printf("\nFont browser done.\n");
	return 0;
}
