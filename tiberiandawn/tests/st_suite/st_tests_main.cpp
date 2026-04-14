/*
 * Atari ST on-target test harness (separate from game main).
 * Build: make st-tests   -> bin/AtariST/cnc_st_tests.tos
 */

#include "st_build_frame_assets.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>

extern int st_run_c2p_autotests(void);
extern int st_run_interactive_gradient(void);
extern int st_run_interactive_htitle(void);
extern int st_run_interactive_title_production_path(void);
extern int st_run_interactive_title_menu_overlay(void);
extern int st_run_interactive_title_mouse_cursor(void);

static void print_banner(void)
{
	printf("\n");
	printf("========================================\n");
	printf("  C&C ST test suite (on-machine)\n");
	printf("========================================\n");
	st_wrap_puts(
			"1 Automated: C2P pack / planar readback "
			"2 Interactive: horizontal ramp (low rez) "
			"3 Interactive: HTITLE from UPDATE.MIX\n"
			"4 Interactive: HTITLE production path (game code) "
			"\n"
			"5 Interactive: HTITLE + main menu overlay (dialog + gradient labels) "
			"\n"
			"6 Interactive: HTITLE + moving mouse cursor (MOUSE.SHP) "
			"\n"
			"7 Interactive: SHP grid (CONQUER.MIX KeyFrame, checker + XOR/LCW) "
			"\n"
			"8 Run automated only (no prompts: C2P + Build_Frame SHP checks) "
			"\n"
			"0 Exit",
			ST_TEXT_MAXCOL);
	printf("Choice: ");
	fflush(stdout);
}

int main(void)
{
	for (;;) {
		print_banner();
		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		switch (ch) {
		case '1':
			printf("\n-- Automated C2P --\n");
			st_run_c2p_autotests();
			break;
		case '2':
			st_run_interactive_gradient();
			break;
		case '3':
			st_run_interactive_htitle();
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
			int r = st_run_c2p_autotests();
			int bf = st_run_build_frame_asset_autocheck();
			printf("Auto C2P: %s\n", r ? "FAIL" : "PASS");
			printf("Auto Build_Frame assets: %s (fail count=%d)\n",
					bf ? "FAIL" : "PASS", bf);
			break;
		}
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
