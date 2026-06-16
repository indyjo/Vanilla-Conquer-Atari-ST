/*
 * Automated: terrain 24x24 left-edge clip via production-style planar atlas packing.
 *
 * Mirrors drawbuff.cpp Try_Blit_Cached_Terrain_Tile / Buffer_Draw_Stamp:
 *   - atlas slots spaced 24px apart (neighbor tile at x+24)
 *   - left clip: clip_src_x, dst_x pinned to viewport origin, reduced width
 *   - ST_Blitter_Planar_Rect_Blit from atlas (no sx = dx & 15 alignment fudge)
 *
 * Reference: C2P_Blit_Linear8_To_Planar on the same linear sub-rectangle.
 * Requires real ST hardware BLiTTER (on-machine st-tests binary).
 */

#include "st_terrain_tile_left_clip_autotest.h"

#include "c2p.h"
#include "palette.h"
#include "st_blitter_blit.h"
#include "st_temperat_palette.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ST_TILE_W = 24,
	ST_TILE_H = 24,
	ST_ATLAS_W = 320,
	ST_ATLAS_H = 24,
	ST_ATLAS_BPL = (ST_ATLAS_W / 16) * 8,
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

static void st_build_production_atlas(
	uint8_t *atlas,
	const unsigned char *tile_a,
	const unsigned char *tile_b)
{
	memset(atlas, 0, (size_t)ST_ATLAS_BPL * (size_t)ST_ATLAS_H);
	C2P_Render_Logical_To_Planar_Rect(
		tile_a, ST_TILE_W, ST_TILE_H, ST_TILE_W,
		atlas, ST_ATLAS_BPL, ST_ATLAS_W, ST_ATLAS_H,
		0, 0, 0, 0);
	C2P_Render_Logical_To_Planar_Rect(
		tile_b, ST_TILE_W, ST_TILE_H, ST_TILE_W,
		atlas, ST_ATLAS_BPL, ST_ATLAS_W, ST_ATLAS_H,
		ST_TILE_W, 0, ST_TILE_W, 0);
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
	const int old_weight_set = C2P_Get_WeightSet();

	Setscreen(-1L, -1L, 0);
	uint8_t *screen_hw = (uint8_t *)Logbase();
	uint8_t *screen_ref = (uint8_t *)malloc(ST_PLANAR_FRAME_BYTES);
	uint8_t *checker_bg = (uint8_t *)malloc(ST_PLANAR_FRAME_BYTES);
	uint8_t *atlas = (uint8_t *)malloc((size_t)ST_ATLAS_BPL * (size_t)ST_ATLAS_H);
	unsigned char tile_a[ST_TILE_W * ST_TILE_H];
	unsigned char tile_b[ST_TILE_W * ST_TILE_H];

	if (!screen_hw || !screen_ref || !checker_bg || !atlas) {
		free(screen_ref);
		free(checker_bg);
		free(atlas);
		C2P_Select_WeightSet(old_weight_set);
		Setscreen(old_log, old_phys, old_rez);
		st_wrap_puts("FAIL: terrain clip test allocation.", ST_TEXT_MAXCOL);
		return 1;
	}

	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	{
		unsigned char pal[768];
		memcpy(pal, kStTemperatPal768, sizeof(pal));
		Set_Palette(pal);
	}

	st_build_synthetic_tile(tile_a, 11);
	st_build_synthetic_tile(tile_b, 29);
	st_build_production_atlas(atlas, tile_a, tile_b);
	st_build_checker_planar(checker_bg, 2, 4);

	int case_failures = 0;
	int printed = 0;
	static const int k_atlas_x[] = { 0, ST_TILE_W };
	static const int k_dx_abs[] = { ST_TAC_PIXEL_X, ST_TAC_PIXEL_X + 8 };

	for (int ai = 0; ai < 2; ai++) {
		const int atlas_x = k_atlas_x[ai];
		for (int clip_src_x = 0; clip_src_x <= ST_MAX_CLIP_SRC_X; clip_src_x++) {
			const int clip_blit_w = ST_TILE_W - clip_src_x;
			if (clip_blit_w <= 0) {
				continue;
			}
			for (int di = 0; di < 2; di++) {
				const int dx_abs = k_dx_abs[di];
				const int dy_abs = 16;
				if (dx_abs + clip_blit_w > ST_PLANAR_WIDTH) {
					continue;
				}

				memcpy(screen_hw, checker_bg, ST_PLANAR_FRAME_BYTES);
				memcpy(screen_ref, checker_bg, ST_PLANAR_FRAME_BYTES);

				const int sx_abs = atlas_x + clip_src_x;
				if (!ST_Blitter_Planar_Rect_Blit(
						atlas,
						ST_ATLAS_BPL,
						sx_abs,
						0,
						screen_hw,
						ST_PLANAR_BYTES_PER_LINE,
						dx_abs,
						dy_abs,
						clip_blit_w,
						ST_TILE_H)) {
					case_failures++;
					if (printed < ST_MAX_MISMATCH_PRINT) {
						printf("  FAIL blit atlas=%d clip=%d dx=%d\n",
							atlas_x, clip_src_x, dx_abs);
						printed++;
					}
					continue;
				}

				for (int ry = 0; ry < ST_TILE_H; ry++) {
					for (int rx = 0; rx < clip_blit_w; rx++) {
						const unsigned char c = ST_Planar_GetPixel(
							atlas,
							ST_ATLAS_BPL,
							ST_ATLAS_W,
							ST_ATLAS_H,
							sx_abs + rx,
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
							"  MISMATCH atlas=%d clip=%d dx=%d sx%%16=%d dx%%16=%d px=%d\n",
							atlas_x,
							clip_src_x,
							dx_abs,
							sx_abs & 15,
							dx_abs & 15,
							mm);
						printed++;
					}
				} else if (verbose) {
					printf("  OK atlas=%d clip=%d dx=%d\n", atlas_x, clip_src_x, dx_abs);
				}
			}
		}
	}

	if (out_mismatches) {
		*out_mismatches = case_failures;
	}

	free(screen_ref);
	free(checker_bg);
	free(atlas);
	C2P_Select_WeightSet(old_weight_set);
	Setscreen(old_log, old_phys, old_rez);

	if (case_failures == 0) {
		printf("  Terrain left-clip atlas: PASS\n");
		return 0;
	}

	printf("  Terrain left-clip atlas: FAIL (%d cases)\n", case_failures);
	return case_failures;
}

int st_run_terrain_tile_left_clip_autotest(void)
{
	return st_run_terrain_tile_left_clip_autotest_ex(0, NULL);
}
