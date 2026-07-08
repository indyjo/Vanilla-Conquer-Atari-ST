/*
 * Automated: terrain 24x24 left-edge clip via ST16 per-icon planar slabs.
 *
 * Mirrors ST16_Blit_Stamp / drawbuff.cpp production path:
 *   - each icon is a 32x24 planar slab (384 bytes), not a shared atlas
 *   - left clip: clip_src_x, reduced width
 *   - ST_Blit_Planar_Rect_Blit from icon planar (clip_src_x into slab)
 *
 * Reference: CPU readback from the same planar icon sub-rectangle.
 * Requires real ST hardware BLiTTER (on-machine st-tests binary).
 */

#include "st_terrain_tile_left_clip_autotest.h"

#include "c2p.h"
#include "palette.h"
#include "st16_iconset.h"
#include "st_blit.h"
#include "st_temperat_palette.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ST_TILE_W = 24,
	ST_TILE_H = 24,
	ST_TILE_PLANAR_W = 32,
	ST_TILE_PLANAR_ROW = (ST_TILE_PLANAR_W / 16) * 8,
	ST_TILE_PLANAR_STRIDE = ST_TILE_PLANAR_ROW * ST_TILE_H,
	ST_TAC_PIXEL_X = 0,
	ST_MAX_CLIP_SRC_X = 22,
	ST_MAX_MISMATCH_PRINT = 16,
	ST_PLANAR_FRAME_BYTES = ST_PLANAR_BYTES_PER_LINE * ST_PLANAR_HEIGHT
};

static void st_build_checker_planar(uint8_t *planar, unsigned char color_a, unsigned char color_b)
{
	for (int y = 0; y < ST_PLANAR_HEIGHT; y++) {
		for (int x = 0; x < ST_PLANAR_WIDTH; x++) {
			const unsigned char c = (((x >> 4) ^ (y >> 4)) & 1) ? color_a : color_b;
			ST_Planar_PutPixel(planar, ST_PLANAR_BYTES_PER_LINE, ST_PLANAR_WIDTH, ST_PLANAR_HEIGHT, x, y, c);
		}
	}
}

static void st_build_synthetic_tile(unsigned char *tile, int seed)
{
	for (int y = 0; y < ST_TILE_H; y++) {
		for (int x = 0; x < ST_TILE_W; x++) {
			tile[y * ST_TILE_W + x] = (unsigned char)(1 + (((x * 5 + y * 3 + seed) ^ (x + seed * 7)) & 15));
		}
	}
}

static void st_build_st16_planar_icon(uint8_t *planar, const unsigned char *tile)
{
	ST16_PlanarLayout layout;

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, FALSE, &layout);
	memset(planar, 0, (size_t)ST_TILE_PLANAR_STRIDE);
	C2P_Render_Logical_To_Planar_Rect(
		tile,
		ST_TILE_W,
		ST_TILE_H,
		ST_TILE_W,
		planar,
		layout.planar_row_bytes,
		layout.planar_w,
		layout.planar_h,
		0,
		0,
		0,
		0);
	ST16_Clear_Planar_Icon_Padding(planar, &layout, ST_TILE_W, ST_TILE_H);
}

static int st_compare_clip_rect(
	const uint8_t *hw,
	const uint8_t *ref,
	int dx_abs,
	int dy_abs,
	int clip_w,
	int clip_h)
{
	int mismatches = 0;
	for (int y = 0; y < clip_h; y++) {
		for (int x = 0; x < clip_w; x++) {
			const int px = dx_abs + x;
			const int py = dy_abs + y;
			const unsigned char got = ST_Planar_GetPixel(
				hw, ST_PLANAR_BYTES_PER_LINE, ST_PLANAR_WIDTH, ST_PLANAR_HEIGHT, px, py);
			const unsigned char exp = ST_Planar_GetPixel(
				ref, ST_PLANAR_BYTES_PER_LINE, ST_PLANAR_WIDTH, ST_PLANAR_HEIGHT, px, py);
			if (got != exp) {
				mismatches++;
			}
		}
	}
	return mismatches;
}

int st_run_terrain_tile_left_clip_autotest_ex(int verbose, int *out_mismatches)
{
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();

	Setscreen(-1L, -1L, 0);
	uint8_t *screen_hw = (uint8_t *)Logbase();
	uint8_t *screen_ref = (uint8_t *)malloc(ST_PLANAR_FRAME_BYTES);
	uint8_t *checker_bg = (uint8_t *)malloc(ST_PLANAR_FRAME_BYTES);
	uint8_t *planar_a = (uint8_t *)malloc((size_t)ST_TILE_PLANAR_STRIDE);
	uint8_t *planar_b = (uint8_t *)malloc((size_t)ST_TILE_PLANAR_STRIDE);
	unsigned char tile_a[ST_TILE_W * ST_TILE_H];
	unsigned char tile_b[ST_TILE_W * ST_TILE_H];

	if (!screen_hw || !screen_ref || !checker_bg || !planar_a || !planar_b) {
		free(screen_ref);
		free(checker_bg);
		free(planar_a);
		free(planar_b);
		Setscreen(old_log, old_phys, old_rez);
		st_wrap_puts("FAIL: terrain clip test allocation.", ST_TEXT_MAXCOL);
		return 1;
	}

	C2P_Load_WeightSet("TEMPERAT", "Terrain clip autotest");
	{
		unsigned char pal[768];
		memcpy(pal, kStTemperatPal768, sizeof(pal));
		Set_Palette(pal);
	}

	st_build_synthetic_tile(tile_a, 11);
	st_build_synthetic_tile(tile_b, 29);
	st_build_st16_planar_icon(planar_a, tile_a);
	st_build_st16_planar_icon(planar_b, tile_b);
	st_build_checker_planar(checker_bg, 2, 4);

	int case_failures = 0;
	int printed = 0;
	const uint8_t *const k_icons[] = { planar_a, planar_b };

	for (int ai = 0; ai < 2; ai++) {
		const uint8_t *planar = k_icons[ai];
		for (int clip_src_x = 0; clip_src_x <= ST_MAX_CLIP_SRC_X; clip_src_x++) {
			const int clip_blit_w = ST_TILE_W - clip_src_x;
			if (clip_blit_w <= 0) {
				continue;
			}
			for (int di = 0; di < 2; di++) {
				const int dx_abs = (di == 0) ? ST_TAC_PIXEL_X : (ST_TAC_PIXEL_X + 8);
				const int dy_abs = 16;
				if (dx_abs + clip_blit_w > ST_PLANAR_WIDTH) {
					continue;
				}

				memcpy(screen_hw, checker_bg, ST_PLANAR_FRAME_BYTES);
				memcpy(screen_ref, checker_bg, ST_PLANAR_FRAME_BYTES);

				if (!ST_Blit_Planar_Rect_Blit(
						planar,
						ST_TILE_PLANAR_ROW,
						clip_src_x,
						0,
						screen_hw,
						ST_PLANAR_BYTES_PER_LINE,
						dx_abs,
						dy_abs,
						clip_blit_w,
						ST_TILE_H)) {
					case_failures++;
					if (printed < ST_MAX_MISMATCH_PRINT) {
						printf("  FAIL blit icon=%d clip=%d dx=%d\n", ai, clip_src_x, dx_abs);
						printed++;
					}
					continue;
				}

				for (int ry = 0; ry < ST_TILE_H; ry++) {
					for (int rx = 0; rx < clip_blit_w; rx++) {
						const unsigned char c = ST_Planar_GetPixel(
							planar,
							ST_TILE_PLANAR_ROW,
							ST_TILE_PLANAR_W,
							ST_TILE_H,
							clip_src_x + rx,
							ry);
						ST_Planar_PutPixel(
							screen_ref,
							ST_PLANAR_BYTES_PER_LINE,
							ST_PLANAR_WIDTH,
							ST_PLANAR_HEIGHT,
							dx_abs + rx,
							dy_abs + ry,
							c);
					}
				}

				const int mm = st_compare_clip_rect(
					screen_hw, screen_ref, dx_abs, dy_abs, clip_blit_w, ST_TILE_H);
				if (mm != 0) {
					case_failures++;
					if (printed < ST_MAX_MISMATCH_PRINT) {
						printf(
							"  MISMATCH icon=%d clip=%d dx=%d sx%%16=%d dx%%16=%d px=%d\n",
							ai,
							clip_src_x,
							dx_abs,
							(clip_src_x) & 15,
							dx_abs & 15,
							mm);
						printed++;
					}
				} else if (verbose) {
					printf("  OK icon=%d clip=%d dx=%d\n", ai, clip_src_x, dx_abs);
				}
			}
		}
	}

	if (out_mismatches) {
		*out_mismatches = case_failures;
	}

	free(screen_ref);
	free(checker_bg);
	free(planar_a);
	free(planar_b);
	Setscreen(old_log, old_phys, old_rez);

	if (case_failures == 0) {
		printf("  Terrain left-clip ST16: PASS\n");
		return 0;
	}

	printf("  Terrain left-clip ST16: FAIL (%d cases)\n", case_failures);
	return case_failures;
}

int st_run_terrain_tile_left_clip_autotest(void)
{
	return st_run_terrain_tile_left_clip_autotest_ex(0, NULL);
}
