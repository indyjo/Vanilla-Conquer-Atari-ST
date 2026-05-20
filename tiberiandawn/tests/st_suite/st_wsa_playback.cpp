/*
 * Interactive: open and play a WSA animation using the shared WSA pipeline.
 */

#include "function.h"
#include "c2p.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_text.h"
#include "wsa.h"

#include <mint/osbind.h>

#include <stdlib.h>
#include <stdio.h>
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

static void st_wait_vblanks(int n)
{
	for (int i = 0; i < n; i++) {
		Vsync();
	}
}

static int st_count_nonzero(const unsigned char *buf, int len)
{
	int n = 0;
	for (int i = 0; i < len; i++) {
		if (buf[i] != 0) {
			n++;
		}
	}
	return n;
}

static void st_draw_palette_grid_16x16(unsigned char *chunky320x200)
{
	const int grid_px = 16 * 8;
	const int x0 = (320 - grid_px) / 2;
	const int y0 = (200 - grid_px) / 2;

	memset(chunky320x200, 0, 320 * 200);
	for (int gy = 0; gy < 16; gy++) {
		for (int gx = 0; gx < 16; gx++) {
			unsigned char c = (unsigned char)(gy * 16 + gx);
			for (int dy = 0; dy < 8; dy++) {
				for (int dx = 0; dx < 8; dx++) {
					chunky320x200[(y0 + gy * 8 + dy) * 320 + (x0 + gx * 8 + dx)] = c;
				}
			}
		}
	}
}

static int st_wsa_is_available(const char *name)
{
	CCFileClass file(name);
	return file.Is_Available();
}

static int st_pick_wsa_name(char *out_name, size_t out_cap)
{
	static const char *kCandidates[] = {
		"CHOOSE.WSA",
		"MLTIPLYR.WSA",
		"GREYERTH.WSA",
		"E-BWTOCL.WSA",
		"EARTH_E.WSA",
		"EARTH_A.WSA",
		"EUROPE.WSA",
		"AFRICA.WSA",
		"BOSNIA.WSA",
		"S_AFRICA.WSA",
		"SCRSCN1.WSA",
		"S-GDIIN2.WSA",
	};
	char avail[64][32];
	int count = (int)(sizeof(kCandidates) / sizeof(kCandidates[0]));
	int avail_count = 0;
	int page = 0;
	const int page_size = 9;

	if (!out_name || out_cap < 6) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		if (!st_wsa_is_available(kCandidates[i])) {
			continue;
		}
		strncpy(avail[avail_count], kCandidates[i], 31);
		avail[avail_count][31] = '\0';
		avail_count++;
		if (avail_count >= 64) {
			break;
		}
	}

	if (avail_count == 0) {
		printf("WSA menu: no selectable WSA is available in currently registered MIX files.\n");
		return 0;
	}

	for (;;) {
		int start = page * page_size;
		int end = start + page_size;
		if (end > avail_count) {
			end = avail_count;
		}

		printf("\n");
		printf("---- WSA selection (%d-%d of %d) ----\n", start + 1, end, avail_count);
		for (int i = start; i < end; i++) {
			printf("%d %s\n", (i - start) + 1, avail[i]);
		}
		printf("n next page, p prev page, 0 cancel\n");
		printf("Choice: ");
		fflush(stdout);

		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == '0' || ch == 27) {
			return 0;
		}
		if (ch == 'n' || ch == 'N') {
			if ((page + 1) * page_size < avail_count) {
				page++;
			}
			continue;
		}
		if (ch == 'p' || ch == 'P') {
			if (page > 0) {
				page--;
			}
			continue;
		}
		if (ch >= '1' && ch <= '9') {
			int idx = start + (ch - '1');
			if (idx >= start && idx < end) {
				strncpy(out_name, avail[idx], out_cap - 1);
				out_name[out_cap - 1] = '\0';
				return 1;
			}
		}
		printf("Unknown option.\n");
	}
}

extern "C" int st_run_interactive_wsa_playback(void)
{
	static unsigned char s_logical[320 * 200];
	char selected_name[32];
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char pal[768];
	int ok = 0;
	void *anim = NULL;
	const char *opened_name = selected_name;
	int has_anim_palette = 0;

	if (!st_pick_wsa_name(selected_name, sizeof(selected_name))) {
		printf("WSA: cancelled.\n");
		return 0;
	}

	printf("WSA: opening %s...\n", selected_name);
	anim = Open_Animation(selected_name,
	                      NULL,
	                      0L,
	                      (WSAOpenType)(WSA_OPEN_FROM_MEM | WSA_OPEN_TO_BUFFER),
	                      pal);
	if (!anim) {
		printf("WSA: open failed (file unavailable or decode init failure): %s\n", selected_name);
		return 0;
	}
	printf("WSA: opened %s\n", opened_name ? opened_name : "(unknown)");

	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	GraphicViewPortClass *old_logic = NULL;
	GraphicBufferClass *screen = NULL;
	GraphicViewPortClass *screen_vp = NULL;
	GraphicBufferClass *logical_buf = NULL;
	GraphicViewPortClass *logical_vp = NULL;
	st_hw_palette_read(saved_hw);

	Setscreen(-1L, -1L, 0);
	unsigned char *tos_screen = (unsigned char *)Logbase();
	screen = new GraphicBufferClass();
	if (!screen) {
		SuperToUser(old_ssp);
		printf("WSA: FAIL OOM setting up screen objects\n");
		return 1;
	}
	screen->Init(320, 200, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
	screen_vp = new GraphicViewPortClass(screen, 0, 0, 320, 200);
	if (!screen_vp) {
		SuperToUser(old_ssp);
		printf("WSA: FAIL OOM setting up screen viewport\n");
		return 1;
	}
	Setscreen((long)screen->Get_Buffer(), (long)screen->Get_Buffer(), -1L);
	old_logic = Set_Logic_Page(screen_vp);
	screen_vp->Clear(0);

	unsigned char *logical = s_logical;
	memset(logical, 0, 320 * 200);
	logical_buf = new GraphicBufferClass();
	if (!logical_buf) {
		Set_Logic_Page(old_logic);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("WSA: FAIL OOM setting up logical buffer\n");
		return 1;
	}
	logical_buf->Init(320, 200, logical, 320L * 200L, (int)GBC_NONE);
	logical_vp = new GraphicViewPortClass(logical_buf, 0, 0, 320, 200);
	if (!logical_vp) {
		Set_Logic_Page(old_logic);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("WSA: FAIL OOM setting up logical viewport\n");
		return 1;
	}

	has_anim_palette = Get_Animation_Palette(anim);
	if (has_anim_palette) {
		Set_Palette(pal);
		printf("WSA: palette present (ST pens 0..15 installed from WSA palette)\n");
	} else {
		printf("WSA: no embedded palette (ST pens 0..15 installed from CurrentPalette)\n");
	}
	/*
	 * Palette sanity view (same 16x16 swatch concept as test 2), but using the
	 * active palette for this WSA path.
	 */
	st_draw_palette_grid_16x16(logical);
	C2P_Render_Logical_To_ST_Screen(logical, 320, tos_screen, 0, C2P_ST_SCREEN_HEIGHT, 1);
	st_wait_vblanks(20);
	memset(logical, 0, 320 * 200);

	int frames = Get_Animation_Frame_Count(anim);
	int aw = Get_Animation_Width(anim);
	int ah = Get_Animation_Height(anim);
	printf("WSA: frames=%d size=%dx%d\n", frames, aw, ah);
	if (frames <= 0) {
		Close_Animation(anim);
		C2P_Clear_CustomWeights();
		Set_Logic_Page(old_logic);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("WSA: FAIL invalid frame count for %s\n", opened_name ? opened_name : "(unknown)");
		return 1;
	}

	/* Play two loops at roughly ~15fps (4 VBL/frame on 60Hz). */
	for (int loop = 0; loop < 2; loop++) {
		for (int f = 0; f < frames; f++) {
			int okf = Animate_Frame(anim, *logical_vp, f, 0, 0, WSA_NORMAL, NULL, NULL);
			if (!okf) {
				int nz = st_count_nonzero(logical, 320 * 200);
				printf("WSA: ERROR frame %d ok=%d nonzero=%d\n", f, okf, nz);
			}
			C2P_Render_Logical_To_ST_Screen(logical, 320, tos_screen, 0, C2P_ST_SCREEN_HEIGHT, 1);
			st_wait_vblanks(4);
		}
	}

	/*
	 * Keep test robust even if decoder touched heap metadata:
	 * avoid teardown allocations/frees here. (Process is short-lived.)
	 */
	/* Close_Animation(anim); */

	ok = st_read_yes_no();

	C2P_Clear_CustomWeights();
	Set_Logic_Page(old_logic);
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);

	printf("WSA: %s (%s)\n", ok ? "PASS" : "FAIL", opened_name ? opened_name : "(unknown)");
	return ok ? 0 : 1;
}
