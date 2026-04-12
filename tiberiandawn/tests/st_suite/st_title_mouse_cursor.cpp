/*
 * Interactive: HTITLE production draw, then animate the game mouse cursor (MOUSE.SHP)
 * over the title using WWMouseClass::Draw_Mouse. Position is synthetic (DLLForceMouseX/Y),
 * stepped once per Vsync (~50 Hz) for natural motion.
 */

#include "function.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_mix_minimal.h"
#include "st_text.h"

#include "ccfile.h"
#include "mouse.h"
#include "shape.h"

#include <math.h>
#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Load_Title_Screen(char *name, GraphicViewPortClass *video_page, unsigned char *palette);
extern void *Load_Alloc_Data(FileClass &file);

#define ST_HW_PAL_COUNT 16
#define ST_PROD_PCX_NAME "ST_HTEST.PCX"
#define ST_MOUSE_MIX_NAME "MOUSE.SHP"
#define ST_MOUSE_TEMP_FILE "ST_MOUSE.SHP"

#define ST_MOUSE_DEMO_FRAMES 400

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

static void st_hw_palette_grey16(void)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++) {
		unsigned short channel = (unsigned short)(((i >> 1) & 0x7) | ((i & 0x1) << 3));
		unsigned short color = (unsigned short)(((channel << 8) | (channel << 4) | channel));
		pr[i] = color;
	}
}

int st_run_interactive_title_mouse_cursor(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	unsigned char pal[768];
	void *mouse_block = NULL;
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	st_hw_palette_read(saved_hw);

	int mx = st_mix_extract_file("UPDATE.MIX", "HTITLE.PCX", &raw, &raw_len);
	if (mx != 0 || !raw) {
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		printf("SKIP: UPDATE.MIX err=%d\n", mx);
		st_wrap_puts("Need UPDATE.MIX with HTITLE.PCX in cwd.", ST_TEXT_MAXCOL);
		return 0;
	}

	FILE *out = fopen(ST_PROD_PCX_NAME, "wb");
	if (!out || fwrite(raw, 1, raw_len, out) != raw_len) {
		free(raw);
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		if (out)
			fclose(out);
		printf("SKIP: cannot write %s\n", ST_PROD_PCX_NAME);
		return 0;
	}
	fclose(out);
	free(raw);
	raw = NULL;

	unsigned char *mouse_raw = NULL;
	size_t mouse_len = 0;
	int mmx = st_mix_extract_file("CCLOCAL.MIX", ST_MOUSE_MIX_NAME, &mouse_raw, &mouse_len);
	if (mmx != 0 || !mouse_raw) {
		remove(ST_PROD_PCX_NAME);
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		printf("SKIP: CCLOCAL.MIX %s err=%d\n", ST_MOUSE_MIX_NAME, mmx);
		st_wrap_puts("Need CCLOCAL.MIX with MOUSE.SHP in cwd.", ST_TEXT_MAXCOL);
		return 0;
	}

	FILE *mf = fopen(ST_MOUSE_TEMP_FILE, "wb");
	if (!mf || fwrite(mouse_raw, 1, mouse_len, mf) != mouse_len) {
		free(mouse_raw);
		remove(ST_PROD_PCX_NAME);
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		if (mf)
			fclose(mf);
		printf("SKIP: cannot write %s\n", ST_MOUSE_TEMP_FILE);
		return 0;
	}
	fclose(mf);
	free(mouse_raw);
	mouse_raw = NULL;

	{
		CCFileClass mfile(ST_MOUSE_TEMP_FILE);
		mouse_block = Load_Alloc_Data(mfile);
	}
	remove(ST_MOUSE_TEMP_FILE);
	if (!mouse_block) {
		remove(ST_PROD_PCX_NAME);
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		printf("SKIP: Load_Alloc_Data failed for mouse shape\n");
		return 0;
	}

	GraphicBufferClass screen(320, 200, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);

	Setscreen(-1L, -1L, 0);
	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

	memset(CurrentPalette, 0x01, 768);
	{
		unsigned char warm[768];
		for (int i = 0; i < 256; i++) {
			unsigned char v = (unsigned char)((i * 63) / 255);
			warm[i * 3 + 0] = warm[i * 3 + 1] = warm[i * 3 + 2] = v;
		}
		Set_Palette(warm);
	}
	vp.Clear(0);

	Load_Title_Screen((char *)ST_PROD_PCX_NAME, &vp, pal);
	Set_Palette(pal);
	st_hw_palette_grey16();

	Set_Logic_Page(&vp);

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
			mouse.Draw_Mouse(&vp);
			Vsync();
		}

		DLLForceMouseX = -1;
		DLLForceMouseY = -1;
	}

	/* Redraw title to clear the cursor (no public undraw API). */
	Load_Title_Screen((char *)ST_PROD_PCX_NAME, &vp, pal);
	Set_Palette(pal);
	st_hw_palette_grey16();

	remove(ST_PROD_PCX_NAME);

	delete[] (char *)mouse_block;
	mouse_block = NULL;

	Vsync();

	printf("\n=== INTERACTIVE: HTITLE + moving mouse cursor ===\n");
	st_wrap_puts(
			"Synthetic path (~50 Hz): MOUSE.SHP frame 0 via Set_Mouse_Cursor + "
			"Draw_Mouse over HTITLE. Requires UPDATE.MIX and CCLOCAL.MIX.",
			ST_TEXT_MAXCOL);

	int ok = st_read_yes_no();

	st_hw_palette_write(saved_hw);
	Setscreen(-1L, -1L, old_rez);
	Super(old_ssp);
	return ok ? 0 : 1;
}
