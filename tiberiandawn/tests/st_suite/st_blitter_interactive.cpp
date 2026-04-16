/*
 * Automated: HTITLE.PCX — per direction & increment (1,2,3): HW restore from offscreen,
 * then 16-step smooth scroll via hardware blit (no keypresses).
 * Validates: after each round, planar nibble at screen center vs hidden origin pixel.
 */

#include "c2p.h"
#include "palette.h"
#include "st_blitter_blit.h"
#include "st_mix_minimal.h"
#include "st_pcx_minimal.h"
#include "st_temperat_palette.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16
#define SMOOTH_STEPS 16

/* Center pixel (0..319, 0..199) for validation */
#define MID_X (((ST_PLANAR_WIDTH) - 1) / 2)
#define MID_Y (((ST_PLANAR_HEIGHT) - 1) / 2)

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

static int st_prepare_320x200_chunky(const unsigned char *src, int w, int h, int src_stride,
		unsigned char **disp, int *disp_stride, unsigned char **out_down_to_free)
{
	*out_down_to_free = NULL;
	if (w == 320 && h == 200) {
		*disp = (unsigned char *)src;
		*disp_stride = src_stride;
		return 0;
	}

	unsigned char *out = (unsigned char *)malloc(320u * 200u);
	if (!out)
		return -1;
	for (int y = 0; y < 200; y++) {
		int sy = (y * h) / 200;
		if (sy >= h)
			sy = h - 1;
		const unsigned char *row = src + sy * src_stride;
		for (int x = 0; x < 320; x++) {
			int sx = (x * w) / 320;
			if (sx >= w)
				sx = w - 1;
			out[y * 320 + x] = row[sx];
		}
	}
	*disp = out;
	*disp_stride = 320;
	*out_down_to_free = out;
	return 0;
}

enum ScrollDir {
	DIR_LEFT = 0,
	DIR_RIGHT,
	DIR_UP,
	DIR_DOWN,
	DIR_UP_LEFT,
	DIR_UP_RIGHT,
	DIR_DOWN_LEFT,
	DIR_DOWN_RIGHT,
	DIR_COUNT
};

static const char *kDirName[DIR_COUNT] = {
	"Left", "Right", "Up", "Down",
	"Up-Left", "Up-Right", "Down-Left", "Down-Right",
};

/*
 * One self-blit step: move framebuffer content by k pixels toward the named direction
 * (exposed edge keeps previous pixels).
 */
static BOOL scroll_step_self(uint8_t *planar, ScrollDir dir, int k)
{
	if (k <= 0 || k >= 320 || k >= 200)
		return FALSE;

	BOOL ok = FALSE;
	switch (dir) {
	case DIR_LEFT:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, k, 0, 0, 0, 320 - k, 200);
		break;
	case DIR_RIGHT:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, 0, 0, k, 0, 320 - k, 200);
		break;
	case DIR_UP:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, 0, k, 0, 0, 320, 200 - k);
		break;
	case DIR_DOWN:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, 0, 0, 0, k, 320, 200 - k);
		break;
	case DIR_UP_LEFT:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, k, k, 0, 0, 320 - k, 200 - k);
		break;
	case DIR_UP_RIGHT:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, 0, k, k, 0, 320 - k, 200 - k);
		break;
	case DIR_DOWN_LEFT:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, k, 0, 0, k, 320 - k, 200 - k);
		break;
	case DIR_DOWN_RIGHT:
		ok = ST_Blitter_Planar_Screen_Rect_Blit(planar, planar, 0, 0, k, k, 320 - k, 200 - k);
		break;
	default:
		break;
	}
	if (ok)
		Vsync();
	return ok;
}

static BOOL restore_from_hidden(const uint8_t *hidden, uint8_t *planar)
{
	BOOL ok = ST_Blitter_Planar_Screen_Rect_Blit(hidden, planar, 0, 0, 0, 0, 320, 200);
	if (ok)
		Vsync();
	return ok;
}

/*
 * Where the color at (mid_x, mid_y) after `steps` scroll steps (each by `inc` in `dir`)
 * originated in the pristine hidden buffer — inverse of one scroll_step_self per step.
 */
static void origin_in_hidden_after_scroll(
	ScrollDir dir, int inc, int steps, int mid_x, int mid_y, int *ox, int *oy)
{
	int x = mid_x;
	int y = mid_y;
	for (int t = 0; t < steps; t++) {
		switch (dir) {
		case DIR_LEFT:
			x += inc;
			break;
		case DIR_RIGHT:
			x -= inc;
			break;
		case DIR_UP:
			y += inc;
			break;
		case DIR_DOWN:
			y -= inc;
			break;
		case DIR_UP_LEFT:
			x += inc;
			y += inc;
			break;
		case DIR_UP_RIGHT:
			x -= inc;
			y += inc;
			break;
		case DIR_DOWN_LEFT:
			x += inc;
			y -= inc;
			break;
		case DIR_DOWN_RIGHT:
			x -= inc;
			y -= inc;
			break;
		default:
			break;
		}
	}
	*ox = x;
	*oy = y;
}

int st_run_interactive_blitter_planar(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char *raw_mix = NULL;
	size_t raw_len = 0;
	unsigned char *pcx = NULL;
	int pw = 0, ph = 0, pstride = 0;
	unsigned char pal[768];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);

	uint8_t *hidden = (uint8_t *)malloc(32000);
	if (!hidden) {
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  FAIL: allocation (hidden)\n");
		return 1;
	}

	int mx = st_mix_extract_file("UPDATE.MIX", "HTITLE.PCX", &raw_mix, &raw_len);
	if (mx != 0 || !raw_mix) {
		free(hidden);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  SKIP: UPDATE.MIX / HTITLE.PCX (mix err=%d)\n", mx);
		return 0;
	}

	int err = st_pcx_load_from_memory(raw_mix, raw_len, &pcx, &pw, &ph, &pstride, pal);
	free(raw_mix);
	raw_mix = NULL;

	if (err != 0 || !pcx || pw <= 0 || ph <= 0) {
		free(hidden);
		free(pcx);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  SKIP: HTITLE decode err=%d w=%d h=%d\n", err, pw, ph);
		return 0;
	}

	unsigned char *disp = NULL;
	int disp_stride = 320;
	unsigned char *down_free = NULL;
	if (st_prepare_320x200_chunky(pcx, pw, ph, pstride, &disp, &disp_stride, &down_free) != 0 || !disp) {
		free(hidden);
		free(pcx);
		free(down_free);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  SKIP: HTITLE downsample alloc failed (w=%d h=%d)\n", pw, ph);
		return 0;
	}

	Setscreen(-1L, -1L, 0);
	unsigned char *planar = (unsigned char *)Logbase();
	if (!planar) {
		free(hidden);
		free(down_free);
		free(pcx);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  FAIL: no Logbase\n");
		return 1;
	}

	C2P_Select_WeightSet(C2P_WEIGHTSET_HTITLE);
	Set_Palette(pal);
	St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal);

	C2P_Render_Logical_To_ST_Screen(disp, disp_stride, planar);
	memcpy(hidden, planar, 32000);

	int any_fail = 0;
	static const int kIncrements[] = { 1, 2, 3 };

	for (int d = 0; d < DIR_COUNT; d++) {
		const ScrollDir dir = (ScrollDir)d;
		for (unsigned ii = 0; ii < sizeof(kIncrements) / sizeof(kIncrements[0]); ii++) {
			const int inc = kIncrements[ii];

			if (!restore_from_hidden(hidden, planar)) {
				printf("  FAIL restore HW blit: %s inc=%d\n", kDirName[d], inc);
				any_fail = 1;
				continue;
			}
			Setscreen((long)planar, (long)planar, -1L);
			Vsync();

			int scroll_ok = 1;
			for (int s = 0; s < SMOOTH_STEPS; s++) {
				if (!scroll_step_self(planar, dir, inc)) {
					printf("  FAIL scroll step %d: %s inc=%d\n", s, kDirName[d], inc);
					any_fail = 1;
					scroll_ok = 0;
					break;
				}
				Setscreen((long)planar, (long)planar, -1L);
				Vsync();
			}

			Setscreen((long)planar, (long)planar, -1L);
			Vsync();
			Vsync();

			if (scroll_ok) {
				int ox = 0;
				int oy = 0;
				origin_in_hidden_after_scroll(dir, inc, SMOOTH_STEPS, MID_X, MID_Y, &ox, &oy);
				if (ox < 0 || ox >= ST_PLANAR_WIDTH || oy < 0 || oy >= ST_PLANAR_HEIGHT) {
					printf("  FAIL center origin OOB: %s inc=%d -> (%d,%d)\n",
							kDirName[d], inc, ox, oy);
					any_fail = 1;
				} else {
					unsigned char got = ST_Planar_GetPixel(planar, MID_X, MID_Y);
					unsigned char exp = ST_Planar_GetPixel(hidden, ox, oy);
					if (got != exp) {
						printf(
								"  FAIL center pixel: %s inc=%d mid(%d,%d) got=%u exp=%u hidden(%d,%d)\n",
								kDirName[d], inc, MID_X, MID_Y,
								(unsigned)got, (unsigned)exp, ox, oy);
						any_fail = 1;
					}
				}
			}
		}
	}

	if (any_fail)
		printf("  Test 9 (HTITLE blitter scroll): FAIL\n");
	else
		printf("  Test 9 (HTITLE blitter scroll): PASS\n");

	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	Super(old_ssp);

	free(hidden);
	free(down_free);
	free(pcx);
	return any_fail ? 1 : 0;
}
