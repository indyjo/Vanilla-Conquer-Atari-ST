/*
 * Interactive: TITLE.CPS production draw, then animate the game mouse cursor (MOUSE.SHP)
 * over the title using the normal mouse update path (Process_Mouse), not direct Draw_Mouse.
 * Position is synthetic (DLLForceMouseX/Y), stepped once per Vsync (~50 Hz).
 */

#include "function.h"
#include "c2p.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_temperat_palette.h"
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
#define ST_TITLE_MIX_NAME "CONQUER.MIX"
#define ST_TITLE_CPS_NAME "TITLE.CPS"
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

int st_run_interactive_title_mouse_cursor(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char pal[768];
	void *mouse_block = NULL;
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);
	Setscreen(-1L, -1L, 0);

	unsigned char *mouse_raw = NULL;
	size_t mouse_len = 0;
	int mmx = st_mix_extract_file("LOCAL.MIX", ST_MOUSE_MIX_NAME, &mouse_raw, &mouse_len);
	if (mmx != 0 || !mouse_raw) {
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("SKIP: LOCAL.MIX %s err=%d\n", ST_MOUSE_MIX_NAME, mmx);
		return 0;
	}

	FILE *mf = fopen(ST_MOUSE_TEMP_FILE, "wb");
	if (!mf || fwrite(mouse_raw, 1, mouse_len, mf) != mouse_len) {
		free(mouse_raw);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
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
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("SKIP: Load_Alloc_Data failed for mouse shape\n");
		return 0;
	}

	unsigned char *tos_screen = (unsigned char *)Logbase();
	GraphicBufferClass screen;
	screen.Init(320, 200, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);

	Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

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

	Set_Logic_Page(&vp);

	{
		WWMouseClass mouse(&vp, 64, 64);
		Set_Mouse_Cursor(0, 0, Extract_Shape(mouse_block, 0));
		Show_Mouse();

		/* Align animation to frame boundary before first draw. */
		Vsync();
		for (int f = 0; f < ST_MOUSE_DEMO_FRAMES; f++) {
			double t = (double)f * (2.0 * M_PI / 280.0);
			int mxp = 160 + (int)(110.0 * sin(t));
			int myp = 100 + (int)(72.0 * sin(t * 1.37 + 0.9));
			DLLForceMouseX = mxp;
			DLLForceMouseY = myp;
			/*
			 * Use the same path as the actual game: movement polling/update in Process_Mouse.
			 * This includes restore of prior background before drawing at the new position.
			 */
			Vsync();
			mouse.Process_Mouse();
		}

		DLLForceMouseX = -1;
		DLLForceMouseY = -1;
	}

	/* Redraw title to clear the cursor (no public undraw API). */
	Load_Title_Screen((char *)ST_TITLE_CPS_NAME, &vp, pal);
	C2P_Select_WeightSet(C2P_WEIGHTSET_HTITLE);
	Set_Palette(pal);
	St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal);

	delete[] (char *)mouse_block;
	mouse_block = NULL;

	Vsync();

	int ok = st_read_yes_no();

	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return ok ? 0 : 1;
}
