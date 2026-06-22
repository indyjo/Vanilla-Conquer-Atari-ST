/*
 * Interactive tests: low rez, Setscreen, human Y/N.
 * Test 2: 16x16 VGA index grid (8x8 px/cell) via C2P; pick a .W16 weight set from cwd.
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
#define ST_W16_SCAN_MAX 32
#define ST_W16_PAGE_SIZE 9

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

static char st_ascii_lower(char ch)
{
	if (ch >= 'A' && ch <= 'Z') {
		return (char)(ch - 'A' + 'a');
	}
	return ch;
}

static int st_name_ends_ext4(const char *name, const char *ext4)
{
	size_t const n = strlen(name);
	if (n < 4 || !ext4) {
		return 0;
	}
	const char *tail = name + n - 4;
	for (int i = 0; i < 4; i++) {
		if (st_ascii_lower(tail[i]) != st_ascii_lower(ext4[i])) {
			return 0;
		}
	}
	return 1;
}

static void st_w16_sort_names(char names[][14], int count)
{
	for (int i = 0; i < count - 1; i++) {
		for (int j = i + 1; j < count; j++) {
			if (strcmp(names[i], names[j]) > 0) {
				char tmp[14];
				memcpy(tmp, names[i], sizeof(tmp));
				memcpy(names[i], names[j], sizeof(tmp));
				memcpy(names[j], tmp, sizeof(tmp));
			}
		}
	}
}

static int st_scan_ext_in_cwd(const char *pattern, const char *ext4, char names[][14], int max_names)
{
	_DTA dta;
	int count = 0;

	Fsetdta(&dta);
	if (Fsfirst(pattern, 0) < 0) {
		return 0;
	}
	for (;;) {
		if ((dta.dta_attribute & FA_DIR) == 0 && st_name_ends_ext4(dta.dta_name, ext4)) {
			if (count < max_names) {
				strncpy(names[count], dta.dta_name, 13);
				names[count][13] = '\0';
				count++;
			}
		}
		if (Fsnext() < 0) {
			break;
		}
	}
	st_w16_sort_names(names, count);
	return count;
}

static int st_scan_w16_in_cwd(char names[][14], int max_names)
{
	return st_scan_ext_in_cwd("*.W16", ".W16", names, max_names);
}

static int st_scan_pal_in_cwd(char names[][14], int max_names)
{
	return st_scan_ext_in_cwd("*.PAL", ".PAL", names, max_names);
}

static int st_install_w16(const char *w16_name)
{
	return C2P_Load_WeightSet(w16_name, "Interactive") ? 1 : 0;
}

static int st_load_pal768(const char *pal_name, unsigned char *pal768)
{
	if (!pal_name || !pal_name[0] || !pal768) {
		return 0;
	}
	CCFileClass file(pal_name);
	if (!file.Is_Available() || !file.Open(READ)) {
		return 0;
	}
	long got = file.Read(pal768, 768L);
	file.Close();
	return got == 768L ? 1 : 0;
}

static int st_show_index_grid(const char *w16_name, const char *pal_name)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	unsigned char *chunky = NULL;
	unsigned char *planar = NULL;
	unsigned char pal[768];
	int ok = 0;

	if (!st_install_w16(w16_name)) {
		printf("  FAIL: cannot load %s\n", w16_name ? w16_name : "(null)");
		return 1;
	}
	if (!st_load_pal768(pal_name, pal)) {
		printf("  FAIL: cannot load %s\n", pal_name ? pal_name : "(null)");
		return 1;
	}

	st_hw_palette_read(saved_hw);
	chunky = (unsigned char *)malloc(320 * 200);
	if (!chunky) {
		printf("  FAIL: allocation\n");
		goto restore;
	}

	Setscreen(-1L, -1L, 0);
	planar = (unsigned char *)Logbase();
	if (!planar) {
		printf("  FAIL: no logbase\n");
		goto restore;
	}

	Set_Palette(pal);

	Palette_Debug_Fill_Index_Grid_Chunky(chunky, 320, 200, 320);
	C2P_Render_Logical_To_ST_Screen(chunky, 320, planar, 0, C2P_ST_SCREEN_HEIGHT, 1);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();
	Vsync();

	printf("16x16 index grid (%s + %s)\n", w16_name, pal_name);
	printf("Press any key...\n");
	fflush(stdout);
	(void)st_read_yes_no();
	ok = 1;

restore:
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	free(chunky);
	return ok ? 0 : 1;
}

static int st_pick_file_index(const char *title, const char names[][14], int count)
{
	int page = 0;
	int const page_count = (count + ST_W16_PAGE_SIZE - 1) / ST_W16_PAGE_SIZE;

	for (;;) {
		int const page_start = page * ST_W16_PAGE_SIZE;
		int page_items = count - page_start;
		if (page_items > ST_W16_PAGE_SIZE) {
			page_items = ST_W16_PAGE_SIZE;
		}

		printf("\n");
		printf("---- %s ----\n", title);
		if (page_count > 1) {
			printf("--- page %d/%d ---\n", page + 1, page_count);
		}
		for (int j = 0; j < page_items; j++) {
			printf("%d %s\n", j + 1, names[page_start + j]);
		}
		if (page_count > 1) {
			if (page > 0) {
				printf("p previous page\n");
			}
			if (page + 1 < page_count) {
				printf("n next page\n");
			}
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
		if (page_count > 1) {
			if ((ch == 'n' || ch == 'N') && page + 1 < page_count) {
				page++;
				continue;
			}
			if ((ch == 'p' || ch == 'P') && page > 0) {
				page--;
				continue;
			}
		}
		if (ch >= '1' && ch <= '9') {
			int pick = ch - '1';
			if (pick >= 0 && pick < page_items) {
				return page_start + pick;
			}
		}
		printf("Unknown option.\n");
	}
}

int st_run_interactive_gradient(void)
{
	char w16_names[ST_W16_SCAN_MAX][14];
	char pal_names[ST_W16_SCAN_MAX][14];
	int const w16_count = st_scan_w16_in_cwd(w16_names, ST_W16_SCAN_MAX);
	int const pal_count = st_scan_pal_in_cwd(pal_names, ST_W16_SCAN_MAX);

	printf("\nColor grid: choose .W16 and .PAL for C2P preview.\n");
	if (w16_count == 0) {
		printf("No .W16 files in cwd.\n");
		return 0;
	}
	if (pal_count == 0) {
		printf("No .PAL files in cwd.\n");
		return 0;
	}
	if (w16_count >= ST_W16_SCAN_MAX) {
		printf("(listing first %d .W16 files)\n", ST_W16_SCAN_MAX);
	}
	if (pal_count >= ST_W16_SCAN_MAX) {
		printf("(listing first %d .PAL files)\n", ST_W16_SCAN_MAX);
	}

	for (;;) {
		int w16_idx = st_pick_file_index("16x16 grid: .W16 in cwd", w16_names, w16_count);
		if (w16_idx < 0) {
			printf("Color grid: done.\n");
			return 0;
		}
		int pal_idx = st_pick_file_index("16x16 grid: .PAL in cwd", pal_names, pal_count);
		if (pal_idx < 0) {
			continue;
		}
		(void)st_show_index_grid(w16_names[w16_idx], pal_names[pal_idx]);
	}
}
