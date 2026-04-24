/*
 * Atari ST on-target test harness (separate from game main).
 * Build: make st-tests   -> bin/AtariST/cnc_st_tests.tos
 */

#include "st_audio_asset_autotest.h"
#include "st_build_frame_assets.h"
#include "st_font_browser.h"
#include "st_mix_register.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>

extern int st_run_c2p_autotests(void);
extern int st_run_interactive_gradient(void);
extern int st_run_interactive_title_production_path(void);
extern int st_run_interactive_title_menu_overlay(void);
extern int st_run_interactive_title_mouse_cursor(void);
extern int st_run_interactive_blitter_planar(void);
extern int st_run_blitter_tile_skew_matrix(void);

static void print_banner(void)
{
	/* Each line <= ST_TEXT_MAXCOL (40); no st_wrap_puts so breaks stay clean. */
	printf("\n");
	printf("========================================\n");
	printf("  C&C ST test suite (on-machine)\n");
	printf("========================================\n");
	printf("1 Auto: C2P planar checksum\n");
	printf("2 Interactive: 16x16 color grid (8x8)\n");
	printf("4 Interactive: TITLE prod path\n");
	printf("5 Interactive: TITLE + menu overlay\n");
	printf("6 Interactive: TITLE + mouse cursor\n");
	printf("7 Interactive: SHP grid (CONQUER.MIX)\n");
	printf("8 Automated: C2P + SHP + .AUD (MIX)\n");
	printf("9 Auto: TITLE 8-way blitter scroll\n");
	printf("b Auto: 24x24 tile skew matrix\n");
	printf("f Interactive: font browser (.FNT)\n");
	printf("a Audio tests (submenu)\n");
	printf("0 Exit\n");
	printf("Choice: ");
	fflush(stdout);
}

static void audio_tests_submenu(void)
{
	/* conterm ($484) is not user-accessible on MiNT with memory protection; stay supervisor
	 * for the whole submenu so push/pop and Crawcin are safe. */
	long const ssp = Super(0L);
	st_conterm_keyclick_mute_push();
	for (;;) {
		printf("\n");
		printf("-------- Audio tests -----------------\n");
		int const n = st_asset_audio_try_count();
		for (int i = 0; i < n; i++) {
			char line[48];
			st_asset_audio_try_label(i, line, sizeof(line));
			printf("%d %s\n", i + 1, line);
		}
		printf("%d First hit in list order\n", n + 1);
		printf("0 Back to main menu\n");
		printf("Choice: ");
		fflush(stdout);

		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == '0' || ch == 27) {
			break;
		}
		if (ch >= '1' && ch <= '9') {
			int pick = ch - '0';
			if (pick >= 1 && pick <= n) {
				printf("\n-- Audio try %d --\n", pick);
				int r = st_run_asset_audio_try_index(pick - 1);
				if (r < 0) {
					printf("Internal error (bad index).\n");
				} else if (r == 1) {
					printf("Audio: FAIL\n");
				} else if (r == 2) {
					printf("Audio: SKIP\n");
				} else {
					printf("Audio: PASS\n");
				}
			} else if (pick == n + 1) {
				printf("\n-- Audio first hit --\n");
				int r = st_run_asset_audio_autotest();
				if (r == 1) {
					printf("Audio: FAIL\n");
				} else if (r == 2) {
					printf("Audio: SKIP\n");
				} else {
					printf("Audio: PASS\n");
				}
			} else {
				printf("Unknown option.\n");
			}
			continue;
		}
		printf("Unknown option.\n");
	}
	st_conterm_keyclick_mute_pop();
	Super(ssp);
}

int main(void)
{
	(void)st_tests_register_mixes_once();

	for (;;) {
		print_banner();
		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		switch (ch) {
		case '1':
			printf("\n-- Automated C2P --\n");
			{
				long old_ssp = Super(0L);
				st_run_c2p_autotests();
				Super(old_ssp);
			}
			break;
		case '2':
			st_run_interactive_gradient();
			break;
		case '4':
			st_run_interactive_title_production_path();
			break;
		case '5':
			st_run_interactive_title_menu_overlay();
			break;
		case '6':
			st_run_interactive_title_mouse_cursor();
			break;
		case '7':
			st_run_interactive_build_frame_xor_grid();
			break;
		case '8': {
			long old_ssp = Super(0L);
			st_conterm_keyclick_mute_push();
			int r = st_run_c2p_autotests();
			int bf = st_run_build_frame_asset_autocheck();
			int au = st_run_asset_audio_autotest();
			st_conterm_keyclick_mute_pop();
			Super(old_ssp);
			printf("Auto C2P: %s\n", r ? "FAIL" : "PASS");
			printf("Auto Build_Frame assets: %s (fail count=%d)\n",
					bf ? "FAIL" : "PASS", bf);
			printf("Auto asset audio: %s\n", (au == 1) ? "FAIL" : ((au == 2) ? "SKIP" : "PASS"));
			break;
		}
		case 'a':
		case 'A':
			audio_tests_submenu();
			break;
		case '9':
			st_run_interactive_blitter_planar();
			break;
		case 'b':
		case 'B':
			st_run_blitter_tile_skew_matrix();
			break;
		case 'f':
		case 'F':
			st_run_interactive_font_browser();
			break;
		case '0':
		case 27: /* ESC */
			printf("Bye.\n");
			return 0;
		default:
			printf("Unknown option.\n");
			break;
		}
	}
}
