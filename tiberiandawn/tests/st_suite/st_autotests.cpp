/*
 * Consolidated automated on-target tests (menu options 1 and 8).
 */

#include "st_autotests.h"
#include "st_audio_asset_autotest.h"
#include "st_build_frame_assets.h"
#include "st_c2p_autotest.h"
#include "st16_convert_autotest.h"
#include "st_terrain_tile_left_clip_autotest.h"

#include <mint/osbind.h>

#include <stdio.h>

static void st_autotest_print_status(const char *name, StAutotestStatus st)
{
	const char *label = "PASS";
	if (st == ST_AUTO_FAIL) {
		label = "FAIL";
	} else if (st == ST_AUTO_SKIP) {
		label = "SKIP";
	}
	printf("  %-12s %s\n", name, label);
}

static StAutotestStatus st_status_from_fail_count(int fails)
{
	if (fails > 0) {
		return ST_AUTO_FAIL;
	}
	return ST_AUTO_PASS;
}

static StAutotestStatus st_status_from_audio_result(int au)
{
	if (au == 1) {
		return ST_AUTO_FAIL;
	}
	if (au == 2) {
		return ST_AUTO_SKIP;
	}
	return ST_AUTO_PASS;
}

int st_run_all_autotests(StAutotestReport *report)
{
	StAutotestReport local;
	if (!report) {
		report = &local;
	}
	report->c2p = ST_AUTO_PASS;
	report->build_frame = ST_AUTO_PASS;
	report->audio = ST_AUTO_PASS;
	report->terrain_clip = ST_AUTO_PASS;
	report->st16_convert = ST_AUTO_PASS;
	report->c2p_checksum = 0;
	report->bf_ok = 0;
	report->bf_skip = 0;
	report->bf_fail = 0;
	report->terrain_clip_failures = 0;
	report->st16_convert_failures = 0;

	printf("\n-- Automated tests --\n");

	const int c2p_fails = st_run_c2p_autotests_ex(0, &report->c2p_checksum);
	report->c2p = st_status_from_fail_count(c2p_fails);

	const int bf_fails = st_run_build_frame_asset_autocheck_ex(
		0, &report->bf_ok, &report->bf_skip, &report->bf_fail, &report->build_frame);

	const int au = st_run_asset_audio_autotest_ex(0);
	report->audio = st_status_from_audio_result(au);

	const int terrain_clip_fails = st_run_terrain_tile_left_clip_autotest_ex(
		0, &report->terrain_clip_failures);
	report->terrain_clip = st_status_from_fail_count(terrain_clip_fails);

	const int st16_fails = st_run_st16_convert_autotest_ex(0, &report->st16_convert_failures);
	report->st16_convert = st_status_from_fail_count(st16_fails);
	(void)bf_fails;

	printf("\n-- Summary --\n");
	st_autotest_print_status("C2P", report->c2p);
	if (report->build_frame == ST_AUTO_SKIP) {
		printf("  Build_Frame  SKIP (no CONQUER.MIX / SHPs)\n");
	} else {
		printf("  Build_Frame  %s (%d ok, %d skip, %d fail)\n",
			report->build_frame == ST_AUTO_FAIL ? "FAIL" : "PASS",
			report->bf_ok,
			report->bf_skip,
			report->bf_fail);
	}
	st_autotest_print_status("Audio", report->audio);
	if (report->terrain_clip == ST_AUTO_FAIL) {
		printf("  TerrainClip  FAIL (%d cases)\n", report->terrain_clip_failures);
	} else {
		st_autotest_print_status("TerrainClip", report->terrain_clip);
	}
	if (report->st16_convert == ST_AUTO_FAIL) {
		printf("  ST16Convert  FAIL (%d checks)\n", report->st16_convert_failures);
	} else {
		st_autotest_print_status("ST16Convert", report->st16_convert);
	}

	int any_fail = (report->c2p == ST_AUTO_FAIL)
		|| (report->build_frame == ST_AUTO_FAIL)
		|| (report->audio == ST_AUTO_FAIL)
		|| (report->terrain_clip == ST_AUTO_FAIL)
		|| (report->st16_convert == ST_AUTO_FAIL);
	printf("Overall: %s\n", any_fail ? "FAIL" : "PASS");
	return any_fail ? 1 : 0;
}
