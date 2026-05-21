#ifndef ST_BUILD_FRAME_ASSETS_H
#define ST_BUILD_FRAME_ASSETS_H

#include "st_autotests.h"

/* Returns count of failed Build_Frame calls (0 = all attempted decodes succeeded). */
int st_run_build_frame_asset_autocheck(void);

/*
 * Autocheck KeyFrame SHPs from CONQUER.MIX. verbose: per-asset SKIP lines.
 * out_status: ST_AUTO_SKIP if mix unreachable, else PASS/FAIL from fail count.
 */
int st_run_build_frame_asset_autocheck_ex(
	int verbose,
	int *out_ok,
	int *out_skip,
	int *out_fail,
	StAutotestStatus *out_status);

/* Low rez: menu-pick SHP, Build_Frame all frames tiled; silent Y/Z vs N on preview. */
int st_run_interactive_build_frame_xor_grid(void);

#endif
