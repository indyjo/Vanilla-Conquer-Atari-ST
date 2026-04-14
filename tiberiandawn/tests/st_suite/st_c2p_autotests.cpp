/*
 * Automated tests: C2P planar pack / readback (no display hardware required).
 */

#include "c2p.h"
#include "palette.h"
#include "st_temperat_palette.h"
#include "st_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Golden planar checksum (diagonal index pattern + TEMPERAT + current
 * c2p_palette_opt_weights). Recompute with host g++ if weights/pattern change.
 */
static const unsigned ST_C2P_AUTOTEST_PLANAR_CHECKSUM = 1475674531u;

int st_run_c2p_autotests(void)
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

	memcpy(pal, kStTemperatPal768, 768);
	Set_Palette(pal);

	for (int y = 0; y < 200; y++) {
		for (int x = 0; x < 320; x++)
			chunky[y * 320 + x] = (unsigned char)((x + y * 3) & 255);
	}

	C2P_Render_Logical_To_ST_Screen(chunky, 320, planar);

	/* Corners should not collapse to identical nibbles for this pattern */
	unsigned char c00 = ST_Planar_GetPixel(planar, 0, 0);
	unsigned char c10 = ST_Planar_GetPixel(planar, 319, 0);
	unsigned char c01 = ST_Planar_GetPixel(planar, 0, 199);
	unsigned char c11 = ST_Planar_GetPixel(planar, 319, 199);
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

	if (sum == ST_C2P_AUTOTEST_PLANAR_CHECKSUM) {
		printf("planar checksum: %u OK\n", sum);
	} else {
		printf("planar checksum: %u FAIL (expect %u)\n", sum, ST_C2P_AUTOTEST_PLANAR_CHECKSUM);
		failures++;
	}

	free(chunky);
	free(planar);
	return failures;
}
