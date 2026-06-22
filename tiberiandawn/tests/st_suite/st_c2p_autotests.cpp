/*
 * Automated tests: C2P planar pack / readback (no display hardware required).
 */

#include "st_c2p_autotest.h"

#include "c2p.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Golden planar checksum (diagonal index pattern + TEMPERAT.PAL +
 * atari-assets/temperat.w16). Recompute on ST if temperat.w16 changes.
 */
static const unsigned ST_C2P_AUTOTEST_PLANAR_CHECKSUM = 1823032384u;

int st_run_c2p_autotests_ex(int verbose, unsigned *out_checksum)
{
	int failures = 0;
	unsigned char pal[768];
	unsigned char *chunky = (unsigned char *)malloc(320 * 200);
	unsigned char *planar = (unsigned char *)calloc(1, 32768);

	if (!chunky || !planar) {
		st_wrap_puts("FAIL: allocation.", ST_TEXT_MAXCOL);
		free(chunky);
		free(planar);
		return 1;
	}

	if (!C2P_Load_WeightSet("TEMPERAT", "C2P autotest")) {
		st_wrap_puts("FAIL: could not load TEMPERAT.W16 (copy atari-assets/*.w16 to cwd).", ST_TEXT_MAXCOL);
		free(chunky);
		free(planar);
		return 1;
	}
	memcpy(pal, kStTemperatPal768, 768);
	Set_Palette(pal);

	/* Clean representability API checks against atari-assets/temperat.w16. */
	{
		uint8_t out_color = 0xEEu;
		if (!C2P_Is_Palette_Index_Clean4(0, &out_color) || out_color != 0) {
			st_wrap_puts("FAIL: C2P_Is_Palette_Index_Clean4 expected pal 0 -> color 0.", ST_TEXT_MAXCOL);
			failures++;
		}
		out_color = 0xA5u;
		if (!C2P_Is_Palette_Index_Clean4(16, &out_color) || out_color != 1) {
			st_wrap_puts("FAIL: C2P_Is_Palette_Index_Clean4 expected pal 16 -> color 1.", ST_TEXT_MAXCOL);
			failures++;
		}
	}

	for (int y = 0; y < 200; y++) {
		for (int x = 0; x < 320; x++)
			chunky[y * 320 + x] = (unsigned char)((x + y * 3) & 255);
	}

	C2P_Render_Logical_To_ST_Screen(chunky, 320, planar, 0, C2P_ST_SCREEN_HEIGHT, 1);

	/* Corners should not collapse to identical nibbles for this pattern */
	unsigned char c00 = ST_Planar_GetPixel(planar, 160, 320, 200, 0, 0);
	unsigned char c10 = ST_Planar_GetPixel(planar, 160, 320, 200, 319, 0);
	unsigned char c01 = ST_Planar_GetPixel(planar, 160, 320, 200, 0, 199);
	unsigned char c11 = ST_Planar_GetPixel(planar, 160, 320, 200, 319, 199);
	if (c00 == c10 && c10 == c01 && c01 == c11) {
		st_wrap_puts(
				"FAIL: planar corners identical (unexpected for this pattern).",
				ST_TEXT_MAXCOL);
		failures++;
	}

	/* Deterministic lightweight checksum on planar buffer */
	unsigned sum = 0;
	for (int i = 0; i < 32000; i++)
		sum = sum * 65599u + planar[i];

	if (out_checksum) {
		*out_checksum = sum;
	}

	if (sum == ST_C2P_AUTOTEST_PLANAR_CHECKSUM) {
		if (verbose) {
			printf("planar checksum: %u OK\n", sum);
		}
	} else {
		printf("planar checksum: %u FAIL (expect %u)\n", sum, ST_C2P_AUTOTEST_PLANAR_CHECKSUM);
		failures++;
	}

	free(chunky);
	free(planar);
	return failures;
}

int st_run_c2p_autotests(void)
{
	unsigned sum = 0;
	return st_run_c2p_autotests_ex(1, &sum);
}
