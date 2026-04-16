/*
 * Atari ST on-target test harness (separate from game main).
 * Build: make st-tests   -> bin/AtariST/cnc_st_tests.tos
 */

#include "st_build_frame_assets.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>

extern int st_run_c2p_autotests(void);
extern int st_run_interactive_gradient(void);
extern int st_run_interactive_htitle(void);
extern int st_run_interactive_title_production_path(void);
extern int st_run_interactive_title_menu_overlay(void);
extern int st_run_interactive_title_mouse_cursor(void);
extern int st_run_interactive_blitter_planar(void);

static void print_banner(void)
{
	/* Each line <= ST_TEXT_MAXCOL (40); no st_wrap_puts so breaks stay clean. */
	printf("\n");
	printf("========================================\n");
	printf("  C&C ST test suite (on-machine)\n");
	printf("========================================\n");
	printf("1 Auto: C2P planar checksum\n");
	printf("2 Interactive: 16x16 color grid (8x8)\n");
	printf("3 Interactive: HTITLE (UPDATE.MIX)\n");
	printf("4 Interactive: HTITLE prod path\n");
	printf("5 Interactive: HTITLE + menu overlay\n");
	printf("6 Interactive: HTITLE + mouse cursor\n");
	printf("7 Interactive: SHP grid (CONQUER.MIX)\n");
	printf("8 Automated: C2P + SHP checks\n");
	printf("9 Auto: HTITLE 8-way blitter scroll\n");
	printf("0 Exit\n");
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
			{
				long old_ssp = Super(0L);
				st_run_c2p_autotests();
				Super(old_ssp);
			}
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
			long old_ssp = Super(0L);
			int r = st_run_c2p_autotests();
			int bf = st_run_build_frame_asset_autocheck();
			Super(old_ssp);
			printf("Auto C2P: %s\n", r ? "FAIL" : "PASS");
			printf("Auto Build_Frame assets: %s (fail count=%d)\n",
					bf ? "FAIL" : "PASS", bf);
			break;
		}
		case '9':
			st_run_interactive_blitter_planar();
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
