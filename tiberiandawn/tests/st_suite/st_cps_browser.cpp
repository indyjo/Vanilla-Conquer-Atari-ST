/*
 * Interactive: browse every CPS file referenced in TD source, each paired with
 * its C2P weight set (.W16 on disk).
 *
 * Embedded-palette CPS: palette comes from Load_Uncompress skip data.
 * Others: external .PAL or map-selection WSA context (EUROPE.WSA, …).
 *
 * Place CONQUER.MIX (and companion .W16 from atari-assets/) next to the test .TOS.
 * Regenerate .W16 on the host: python3 tools/palette-opt/gen_cps_w16.py
 */

#include "function.h"
#include "c2p.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_mix_register.h"
#include "st_text.h"
#include "st_test_linkage.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16

typedef struct {
	const char *cps_name;
	const char *w16_name;
	const char *ext_pal_name;
	int embedded_palette;
	const char *context_note;
} StCpsCatalogEntry;

/*
 * All .CPS assets referenced in Tiberian Dawn sources (mix2.txt / MAPSEL / ENDING / INIT).
 */
static const StCpsCatalogEntry kCpsCatalog[] = {
	{"TITLE.CPS", "TITLE.W16", NULL, 1, "title + menus; embedded PAL"},
	{"ATTRACT2.CPS", "ATTRACT2.W16", NULL, 1, "RA teaser; embedded PAL"},
	{"SATSEL.CPS", "SATSEL.W16", "SATSEL.PAL", 0, "NOD sat map; SATSEL.PAL"},
	{"CLICK_E.CPS", "EUROPE.W16", NULL, 0, "GDI map pick; EUROPE.WSA ctx"},
	{"CLICK_EB.CPS", "BOSNIA.W16", NULL, 0, "GDI last scen; BOSNIA.WSA ctx"},
	{"CLICK_A.CPS", "AFRICA.W16", NULL, 0, "NOD map pick; AFRICA.WSA ctx"},
	{"CLICK_SA.CPS", "S_AFRICA.W16", NULL, 0, "NOD last scen; S_AFRICA.WSA ctx"},
};

static const int kCpsCatalogCount = (int)(sizeof(kCpsCatalog) / sizeof(kCpsCatalog[0]));

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

static int st_cps_is_available(const char *name)
{
	CCFileClass file(name);
	return file.Is_Available();
}

static int st_load_pal768(const char *name, unsigned char *pal)
{
	CCFileClass file(name);
	if (!file.Is_Available()) {
		return 0;
	}
	if (!file.Open(READ)) {
		return 0;
	}
	if (file.Read(pal, 768L) != 768L) {
		file.Close();
		return 0;
	}
	file.Close();
	return 1;
}

static int st_try_install_w16(const char *w16_name)
{
	return C2P_Load_WeightSet(w16_name, "CPS") ? 1 : 0;
}

static int st_pick_cps_index(void)
{
	int avail[16];
	int avail_count = 0;

	for (int i = 0; i < kCpsCatalogCount; i++) {
		if (!st_cps_is_available(kCpsCatalog[i].cps_name)) {
			continue;
		}
		if (avail_count < 16) {
			avail[avail_count++] = i;
		}
	}

	if (avail_count == 0) {
		printf("CPS: none of the catalog files are available.\n");
		printf("(Need CONQUER.MIX or loose .CPS in cwd.)\n");
		return -1;
	}

	for (;;) {
		printf("\n");
		printf("---- CPS / W16 pairs (%d available) ----\n", avail_count);
		for (int j = 0; j < avail_count; j++) {
			const StCpsCatalogEntry *e = &kCpsCatalog[avail[j]];
			printf("%d %s -> %s\n", j + 1, e->cps_name, e->w16_name);
			printf("   %s\n", e->context_note);
		}
		printf("0 cancel\n");
		printf("Choice: ");
		fflush(stdout);

		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == '0' || ch == 27) {
			return -1;
		}
		if (ch >= '1' && ch <= '9') {
			int pick = ch - '1';
			if (pick >= 0 && pick < avail_count) {
				return avail[pick];
			}
		}
		printf("Unknown option.\n");
	}
}

static int st_draw_cps_entry(int catalog_index)
{
	const StCpsCatalogEntry *e = &kCpsCatalog[catalog_index];
	unsigned char pal[768];
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	int weight_src = 0;
	int ok = 0;

	memset(pal, 0, sizeof(pal));
	memset(CurrentPalette, 0x01, 768);

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
	vp.Clear(0);

	weight_src = st_try_install_w16(e->w16_name);

	if (e->ext_pal_name && !e->embedded_palette) {
		if (!st_load_pal768(e->ext_pal_name, pal)) {
			printf("CPS: missing external palette %s\n", e->ext_pal_name);
			goto restore;
		}
	}

	if (strcmp(e->cps_name, "TITLE.CPS") == 0) {
		unsigned char warm[768];
		memcpy(warm, kStTemperatPal768, 768);
		Set_Palette(warm);
		Load_Title_Screen((char *)e->cps_name, &vp, pal);
	} else {
		CCFileClass file(e->cps_name);
		void *pal_ptr = e->embedded_palette ? (void *)pal : NULL;
		if (!file.Is_Available()) {
			printf("CPS: file vanished: %s\n", e->cps_name);
			goto restore;
		}
		Load_Uncompress(file, SysMemPage, SysMemPage, pal_ptr);
		if (e->embedded_palette) {
			Set_Palette(pal);
		} else if (e->ext_pal_name) {
			Set_Palette(pal);
		} else {
			memcpy(pal, kStTemperatPal768, 768);
			Set_Palette(pal);
		}

		const int lin_stride = SysMemPage.Get_Width() + SysMemPage.Get_Pitch();
		if (lin_stride > 0 && SysMemPage.Get_Buffer()) {
			C2P_Render_Logical_To_ST_Screen(
				(const uint8_t *)SysMemPage.Get_Buffer(),
				lin_stride,
				(uint8_t *)screen.Get_Buffer(),
				0,
				C2P_ST_SCREEN_HEIGHT,
				1);
		}
	}

	Vsync();
	Vsync();
	printf("Showing %s (weights: %s", e->cps_name, e->w16_name);
	if (weight_src) {
		printf(", .W16 loaded)\n");
	} else {
		printf(" — missing or invalid %s)\n", e->w16_name);
	}
	printf("Press any key...\n");
	fflush(stdout);
	(void)Crawcin();
	ok = 1;

restore:
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return ok ? 0 : 1;
}

extern "C" int st_run_interactive_cps_browser(void)
{
	(void)st_tests_register_mixes_once();

	printf("\nCPS browser: catalog %d entries (source scan).\n", kCpsCatalogCount);

	for (;;) {
		int idx = st_pick_cps_index();
		if (idx < 0) {
			printf("CPS: done.\n");
			return 0;
		}
		(void)st_draw_cps_entry(idx);
	}
}
