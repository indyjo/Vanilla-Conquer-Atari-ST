/*
 * Interactive: decode several SHP frames with Build_Frame (XOR / LCW paths) and
 * show a 320x200 chunky grid on screen for visual confirmation.
 *
 * Assets are chosen so some frames typically exercise XOR chain / Mem_Copy paths
 * (e.g. E1 infantry) while others stay simpler (POWER, MINIGUN, FIRE1).
 *
 * MOUSE.SHP (CCLOCAL) is a multi-shape container (Extract_Shape), not KeyFrame data;
 * Build_Frame applies only to KeyFrame SHPs from CONQUER.MIX here.
 *
 * Requires CONQUER.MIX in cwd.
 */

#include "function.h"
#include "palette.h"
#include "st_build_frame_assets.h"
#include "st_mix_minimal.h"
#include "st_text.h"

#include "c2p.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16
#define ST_SCR_W 320
#define ST_SCR_H 200
#define ST_GAP 2
#define ST_BORDER 1
/* Logical palette indices (grey ramp below): two mid greys for 4x4 checker behind tiles. */
#define ST_CHK_GREY_A ((unsigned char)80)
#define ST_CHK_GREY_B ((unsigned char)160)

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

static void st_hw_palette_grey16(void)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++) {
		unsigned short channel = (unsigned short)(((i >> 1) & 0x7) | ((i & 0x1) << 3));
		unsigned short color = (unsigned short)(((channel << 8) | (channel << 4) | channel));
		pr[i] = color;
	}
}

typedef struct {
	const char *mix;
	const char *shp;
	const char *desc;
	const unsigned short *frames;
	int nframes;
} StBfAsset;

/* Frames that often hit XOR chain / Mem_Copy on unit SHPs; 0 and small ids for sanity. */
static const unsigned short k_frames_e1[] = { 0, 2, 3, 6, 9, 15, 19, 23, 31 };
static const unsigned short k_frames_e2[] = { 0, 3, 7, 11, 15, 21 };
static const unsigned short k_frames_power[] = { 0, 1, 2, 3 };
static const unsigned short k_frames_minigun[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_fire1[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

static const StBfAsset k_assets[] = {
	{ "CONQUER.MIX", "E1.SHP", "infantry E1 (long XOR chain)", k_frames_e1,
		(int)(sizeof(k_frames_e1) / sizeof(k_frames_e1[0])) },
	{ "CONQUER.MIX", "E2.SHP", "infantry E2 (long XOR chain)", k_frames_e2,
		(int)(sizeof(k_frames_e2) / sizeof(k_frames_e2[0])) },
	{ "CONQUER.MIX", "POWER.SHP", "POWER icon (short)", k_frames_power,
		(int)(sizeof(k_frames_power) / sizeof(k_frames_power[0])) },
	{ "CONQUER.MIX", "MINIGUN.SHP", "MINIGUN muzzle", k_frames_minigun,
		(int)(sizeof(k_frames_minigun) / sizeof(k_frames_minigun[0])) },
	{ "CONQUER.MIX", "FIRE1.SHP", "FIRE1 ground fire", k_frames_fire1,
		(int)(sizeof(k_frames_fire1) / sizeof(k_frames_fire1[0])) },
};

static void st_fill_checkerboard_4x4(unsigned char *screen, int scr_w, int scr_h, int scr_stride)
{
	for (int y = 0; y < scr_h; y++) {
		for (int x = 0; x < scr_w; x++) {
			int cell = ((x >> 2) ^ (y >> 2)) & 1;
			screen[y * scr_stride + x] = cell ? ST_CHK_GREY_B : ST_CHK_GREY_A;
		}
	}
}

static void st_blit_tile(unsigned char *screen, int sx, int sy, int scr_stride,
		const unsigned char *src, int w, int h, int src_stride, int skip_zero_transp)
{
	for (int yy = 0; yy < h; yy++) {
		if (sy + yy < 0 || sy + yy >= ST_SCR_H)
			continue;
		for (int xx = 0; xx < w; xx++) {
			if (sx + xx < 0 || sx + xx >= ST_SCR_W)
				continue;
			unsigned char p = src[yy * src_stride + xx];
			if (skip_zero_transp && p == 0)
				continue;
			screen[(sy + yy) * scr_stride + (sx + xx)] = p;
		}
	}
}

static void st_draw_hollow_rect(unsigned char *screen, int x0, int y0, int x1, int y1,
		int scr_stride, unsigned char pen)
{
	for (int x = x0; x <= x1; x++) {
		if (x >= 0 && x < ST_SCR_W && y0 >= 0 && y0 < ST_SCR_H)
			screen[y0 * scr_stride + x] = pen;
		if (x >= 0 && x < ST_SCR_W && y1 >= 0 && y1 < ST_SCR_H)
			screen[y1 * scr_stride + x] = pen;
	}
	for (int y = y0; y <= y1; y++) {
		if (x0 >= 0 && x0 < ST_SCR_W && y >= 0 && y < ST_SCR_H)
			screen[y * scr_stride + x0] = pen;
		if (x1 >= 0 && x1 < ST_SCR_W && y >= 0 && y < ST_SCR_H)
			screen[y * scr_stride + x1] = pen;
	}
}

/*
 * Returns number of failed Build_Frame calls (0 = all decodes returned non-NULL).
 */
int st_run_build_frame_asset_autocheck(void)
{
	int fails = 0;
	for (size_t ai = 0; ai < sizeof(k_assets) / sizeof(k_assets[0]); ai++) {
		const StBfAsset *a = &k_assets[ai];
		unsigned char *raw = NULL;
		size_t raw_len = 0;
		int mx = st_mix_extract_file(a->mix, a->shp, &raw, &raw_len);
		if (mx != 0 || !raw) {
			printf("SKIP %s:%s err=%d\n", a->mix, a->shp, mx);
			continue;
		}
		unsigned short tw = Get_Build_Frame_Width(raw);
		unsigned short th = Get_Build_Frame_Height(raw);
		unsigned short tc = Get_Build_Frame_Count(raw);
		if (tw == 0 || th == 0 || tc == 0) {
			printf("SKIP %s:%s bad header\n", a->mix, a->shp);
			free(raw);
			continue;
		}
		size_t need = (size_t)tw * (size_t)th;
		unsigned char *buf = (unsigned char *)malloc(need);
		if (!buf) {
			free(raw);
			fails++;
			continue;
		}
		for (int fi = 0; fi < a->nframes; fi++) {
			unsigned short fr = a->frames[fi];
			if (fr >= tc)
				continue;
			unsigned long r = Build_Frame(raw, fr, buf);
			if (!r) {
				printf("FAIL Build_Frame %s:%s fr=%u\n", a->mix, a->shp, (unsigned)fr);
				fails++;
			}
		}
		free(buf);
		free(raw);
	}
	return fails;
}

int st_run_interactive_build_frame_xor_grid(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	st_hw_palette_read(saved_hw);

	unsigned char *chunky = (unsigned char *)calloc(1, (size_t)ST_SCR_W * ST_SCR_H);
	unsigned char *planar = (unsigned char *)calloc(1, 32768u);
	if (!chunky || !planar) {
		free(chunky);
		free(planar);
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		printf("FAIL: oom chunky/planar\n");
		return 1;
	}

	st_fill_checkerboard_4x4(chunky, ST_SCR_W, ST_SCR_H, ST_SCR_W);

	printf("\n-- Build_Frame asset grid --\n");
	int pen = 15;
	int cx = ST_GAP;
	int cy = ST_GAP;
	int row_h = 0;

	for (size_t ai = 0; ai < sizeof(k_assets) / sizeof(k_assets[0]); ai++) {
		const StBfAsset *a = &k_assets[ai];
		unsigned char *raw = NULL;
		size_t raw_len = 0;
		int mx = st_mix_extract_file(a->mix, a->shp, &raw, &raw_len);
		if (mx != 0 || !raw) {
			printf("SKIP %s:%s err=%d (%s)\n", a->mix, a->shp, mx, a->desc);
			continue;
		}

		unsigned short tw = Get_Build_Frame_Width(raw);
		unsigned short th = Get_Build_Frame_Height(raw);
		unsigned short tc = Get_Build_Frame_Count(raw);
		if (tw == 0 || th == 0 || tc == 0) {
			printf("SKIP %s:%s bad dims\n", a->mix, a->shp);
			free(raw);
			continue;
		}

		size_t need = (size_t)tw * (size_t)th;
		unsigned char *buf = (unsigned char *)malloc(need);
		if (!buf) {
			free(raw);
			continue;
		}

		printf("%s / %s (%ux%u x%u fr) ", a->mix, a->shp, (unsigned)tw, (unsigned)th,
				(unsigned)tc);

		for (int fi = 0; fi < a->nframes; fi++) {
			unsigned short fr = a->frames[fi];
			if (fr >= tc)
				continue;

			memset(buf, 0, need);
			unsigned long br = Build_Frame(raw, fr, buf);
			if (!br) {
				printf("[f%u=X] ", (unsigned)fr);
				continue;
			}

			if (cx + tw + ST_GAP > ST_SCR_W) {
				cx = ST_GAP;
				cy += row_h + ST_GAP * 2;
				row_h = 0;
			}
			if (cy + th + ST_GAP > ST_SCR_H) {
				printf("(clip) ");
				break;
			}

			st_blit_tile(chunky, cx, cy, ST_SCR_W, buf, tw, th, tw, 1);
			st_draw_hollow_rect(chunky, cx - ST_BORDER, cy - ST_BORDER,
					cx + tw + ST_BORDER - 1, cy + th + ST_BORDER - 1, ST_SCR_W,
					(unsigned char)pen);
			printf("f%u ", (unsigned)fr);

			cx += tw + ST_GAP * 2;
			if (th > row_h)
				row_h = th;
		}
		printf("\n");
		free(buf);
		free(raw);
	}

	{
		unsigned char pal[768];
		for (int i = 0; i < 256; i++) {
			unsigned char v = (unsigned char)((i * 63) / 255);
			pal[i * 3 + 0] = pal[i * 3 + 1] = pal[i * 3 + 2] = v;
		}
		Set_Palette(pal);
	}

	Setscreen(-1L, -1L, 0);
	st_hw_palette_grey16();
	C2P_Render_Logical_To_ST_Screen(chunky, ST_SCR_W, planar);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();
	Vsync();

	printf("Screen: 4x4 grey checker + SHP tiles (border). 'X' in log = Build_Frame failed.\n");
	st_wrap_puts(
			"E1/E2: XOR chain. POWER/MINIGUN/FIRE1: other KeyFrame SHPs. Index 0 left transparent.",
			ST_TEXT_MAXCOL);

	int ok = st_read_yes_no();

	free(chunky);
	free(planar);

	st_hw_palette_write(saved_hw);
	Setscreen(-1L, -1L, old_rez);
	Super(old_ssp);
	return ok ? 0 : 1;
}
