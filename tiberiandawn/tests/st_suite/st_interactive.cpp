/*
 * Interactive tests: low rez, Setscreen, human Y/N.
 */

#include "function.h"
#include "c2p.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_mix_minimal.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16

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

int st_run_interactive_gradient(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);

	unsigned char *chunky = (unsigned char *)malloc(320 * 200);
	unsigned char *planar = NULL;
	unsigned char pal[768];

	if (!chunky) {
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("  FAIL: allocation\n");
		return 1;
	}

	Setscreen(-1L, -1L, 0); /* low rez */
	planar = (unsigned char *)Logbase();
	if (!planar) {
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("  FAIL: no logbase\n");
		return 1;
	}

	memcpy(pal, kStTemperatPal768, 768);
	/* Weight LUTs (and C2P_HW_Palette_Subset) before Set_Palette so STE pens match C2P mapping. */
	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	Set_Palette(pal);

	Palette_Debug_Fill_Index_Grid_Chunky(chunky, 320, 200, 320);

	C2P_Render_Logical_To_ST_Screen(chunky, 320, planar, 0, C2P_ST_SCREEN_HEIGHT, 1);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();

	int ok = st_read_yes_no();

	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);

	free(chunky);
	return ok ? 0 : 1;
}

