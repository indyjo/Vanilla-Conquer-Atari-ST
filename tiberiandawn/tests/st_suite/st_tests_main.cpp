/*
 * Atari ST on-target test harness (separate from game main).
 * Build: make st-tests   -> bin/AtariST/cnc_st_tests.tos
 */

#include "st_audio_asset_autotest.h"
#include "st_audio_mix_test.h"
#include "st_autotests.h"
#include "st_build_frame_assets.h"
#include "st_font_browser.h"
#include "st_mix_register.h"
#include "st_planar_line_bench.h"
#include "st_wsa_playback.h"
#include "st_cps_browser.h"
#include "st_terrain_tile_left_clip_autotest.h"
#include "st_blitter_rect_interactive.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>

static int st_run_automated_bundle(void)
{
	long old_ssp = Super(0L);
	st_conterm_keyclick_mute_push();
	StAutotestReport report;
	int const rc = st_run_all_autotests(&report);
	st_conterm_keyclick_mute_pop();
	SuperToUser(old_ssp);
	return rc;
}
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
	printf("1 Auto: C2P + SHP + .AUD\n");
	printf("2 Interactive: 16x16 grid; pick .W16\n");
	printf("4 Interactive: TITLE prod path\n");
	printf("5 Interactive: TITLE + menu overlay\n");
	printf("6 Interactive: TITLE + mouse cursor\n");
	printf("7 Interactive: SHP grid (CONQUER.MIX)\n");
	printf("8 Auto: same as 1\n");
	printf("9 Auto: TITLE 8-way blitter scroll\n");
	printf("b Auto: 24x24 tile skew matrix\n");
	printf("t Auto: terrain left-clip atlas\n");
	printf("f Interactive: font browser (.FNT)\n");
	printf("w Interactive: WSA playback\n");
	printf("c Interactive: CPS / W16 browser\n");
	printf("l Auto: planar line benchmark\n");
	printf("a Audio tests (submenu)\n");
	printf("i Interactive: blit rect tuner\n");
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
		if (n < 9) {
			printf("%d First hit in list order\n", n + 1);
		} else {
			printf("f First hit in list order\n");
		}
		printf("m Dual-sample mix (2 voices, volumes)\n");
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
			} else if (n < 9 && pick == n + 1) {
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
		if ((ch == 'f' || ch == 'F') && n >= 9) {
			printf("\n-- Audio first hit --\n");
			int r = st_run_asset_audio_autotest();
			if (r == 1) {
				printf("Audio: FAIL\n");
			} else if (r == 2) {
				printf("Audio: SKIP\n");
			} else {
				printf("Audio: PASS\n");
			}
			continue;
		}
		if (ch == 'm' || ch == 'M') {
			(void)st_run_interactive_audio_dual_mix();
			continue;
		}
		printf("Unknown option.\n");
	}
	st_conterm_keyclick_mute_pop();
	SuperToUser(ssp);
}

static int st_read_auto_cmd_char(void)
{
	FILE *f = fopen("TST_AUTO.CMD", "r");
	if (!f) {
		return 0;
	}
	int ch = fgetc(f);
	fclose(f);
	remove("TST_AUTO.CMD");
	if (ch == '\r' || ch == '\n' || ch == EOF) {
		return 0;
	}
	if (ch >= 'A' && ch <= 'Z') {
		ch += 'a' - 'A';
	}
	return ch;
}

static int st_dispatch_menu_choice(int ch)
{
	switch (ch) {
	case '1':
		return st_run_automated_bundle();
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
	case '8':
		return st_run_automated_bundle();
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
	case 't':
	case 'T': {
		long old_ssp = Super(0L);
		int const rc = st_run_terrain_tile_left_clip_autotest_ex(1, NULL);
		SuperToUser(old_ssp);
		return rc;
	}
	case 'f':
	case 'F':
		st_run_interactive_font_browser();
		break;
	case 'w':
	case 'W':
		st_run_interactive_wsa_playback();
		break;
	case 'c':
	case 'C':
		st_run_interactive_cps_browser();
		break;
	case 'i':
	case 'I':
		st_run_interactive_blitter_rect();
		break;
	case 'l':
	case 'L':
		return st_run_planar_line_bench();
	case '0':
	case 27: /* ESC */
		printf("Bye.\n");
		return 0;
	default:
		printf("Unknown option.\n");
		break;
	}
	return 0;
}

int main(void)
{
	(void)st_tests_register_mixes_once();

	int const auto_ch = st_read_auto_cmd_char();
	if (auto_ch) {
		printf("Auto-run: %c\n", auto_ch);
		int const rc = st_dispatch_menu_choice(auto_ch);
		if (auto_ch == '1' || auto_ch == '8' || auto_ch == 't' || auto_ch == 'l') {
			Pterm0();
			return rc;
		}
	}

	for (;;) {
		print_banner();
		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == '0' || ch == 27) {
			printf("Bye.\n");
			return 0;
		}
		(void)st_dispatch_menu_choice(ch);
	}
}
