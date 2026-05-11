/*
 * Interactive: draw TITLE.CPS using the same Load_Title_Screen path as the game.
 */

#include "function.h"
#include "c2p.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_mix_minimal.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Load_Title_Screen(char *name, GraphicViewPortClass *video_page, unsigned char *palette);

#define ST_HW_PAL_COUNT 16
#define ST_TITLE_MIX_NAME "CONQUER.MIX"
#define ST_TITLE_CPS_NAME "TITLE.CPS"

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

int st_run_interactive_title_production_path(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char pal[768];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);
	Setscreen(-1L, -1L, 0);

	unsigned char *tos_screen = (unsigned char *)Logbase();
	GraphicBufferClass screen;
	screen.Init(320, 200, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);

	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

	/*
	 * INIT.CPP uses Set_Palette(GamePalette) long before title-screen draw, then memset(CurrentPalette,1)
	 * immediately before Load_Title_Screen. Prime CurrentPalette with TEMPERAT.PAL so title
	 * palette transitions start from the same state as the game.
	 */
	memset(CurrentPalette, 0x01, 768);
	{
		unsigned char warm[768];
		memcpy(warm, kStTemperatPal768, 768);
		Set_Palette(warm);
	}
	vp.Clear(0);

	Load_Title_Screen((char *)ST_TITLE_CPS_NAME, &vp, pal);

	C2P_Select_WeightSet(C2P_WEIGHTSET_HTITLE);
	Set_Palette(pal);
	St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal);

	Vsync();

	int ok = st_read_yes_no();

	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return ok ? 0 : 1;
}
