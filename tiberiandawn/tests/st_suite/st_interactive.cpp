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
		Super(old_ssp);
		printf("  FAIL: allocation\n");
		return 1;
	}

	Setscreen(-1L, -1L, 0); /* low rez */
	planar = (unsigned char *)Logbase();
	if (!planar) {
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  FAIL: no logbase\n");
		return 1;
	}

	memcpy(pal, kStTemperatPal768, 768);
	Set_Palette(pal);
	St_HW_Palette_Write_Temperat_First16(ST_HW_PALETTE_REGS);

	/* 16x16 cells, 8x8 px each: logical colors 0..255; 128x128 grid bottom-centered. */
	memset(chunky, 0, (size_t)(320 * 200));
	const int grid_px = 16 * 8;
	const int x0 = (320 - grid_px) / 2;
	const int y0 = 200 - grid_px;
	for (int gy = 0; gy < 16; gy++) {
		for (int gx = 0; gx < 16; gx++) {
			unsigned char c = (unsigned char)(gy * 16 + gx);
			for (int dy = 0; dy < 8; dy++) {
				for (int dx = 0; dx < 8; dx++)
					chunky[(y0 + gy * 8 + dy) * 320 + (x0 + gx * 8 + dx)] = c;
			}
		}
	}

	C2P_Render_Logical_To_ST_Screen(chunky, 320, planar);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();

	int ok = st_read_yes_no();

	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	Super(old_ssp);

	free(chunky);
	return ok ? 0 : 1;
}

