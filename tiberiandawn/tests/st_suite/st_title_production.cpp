/*
 * Interactive: draw HTITLE using the same Load_Title_Screen + Read_PCX_File + C2P/Scale
 * path as the game (ATARILIB/title_pcx_load.cpp). Writes a temp PCX for CCFileClass.
 */

#include "function.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_mix_minimal.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Load_Title_Screen(char *name, GraphicViewPortClass *video_page, unsigned char *palette);

#define ST_HW_PAL_COUNT 16
#define ST_PROD_PCX_NAME "ST_HTEST.PCX"

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

int st_run_interactive_title_production_path(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	unsigned char pal[768];
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

	GraphicBufferClass screen(320, 200, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);

	Setscreen(-1L, -1L, 0);
	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

	/*
	 * INIT.CPP uses Set_Palette(GamePalette) long before HTITLE, then memset(CurrentPalette,1)
	 * immediately before Load_Title_Screen. We have no MIX-backed TEMPERAT.PAL here; prime
	 * CurrentPalette with a neutral ramp so C2P / Scale inside Load_Title_Screen see valid
	 * PaletteToST (cold start after only the menu would leave stale tables -> black screen).
	 */
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
	remove(ST_PROD_PCX_NAME);

	Set_Palette(pal);
	st_hw_palette_grey16();

	Vsync();

	printf("\n=== INTERACTIVE: HTITLE production path ===\n");
	st_wrap_puts(
			"Startup-style draw only: Read_PCX_File + "
			"Load_Title_Screen (C2P or Scale). Not the full "
			"menu UI (buttons/dialogs need MENUS). Compare to 3.",
			ST_TEXT_MAXCOL);

	int ok = st_read_yes_no();

	st_hw_palette_write(saved_hw);
	Setscreen(-1L, -1L, old_rez);
	Super(old_ssp);
	return ok ? 0 : 1;
}
