/*
 * Interactive tests: low rez, Setscreen, human Y/N (gradient + HTITLE from UPDATE.MIX).
 */

#include "c2p.h"
#include "palette.h"
#include "st_mix_minimal.h"
#include "st_pcx_minimal.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16

/*
 * C2P is fixed 320x200. PCX may be larger (e.g. 640x400 HTITLE); nearest-neighbour
 * shrink into *out_down (malloc). If already 320x200, *out_down is NULL and *disp
 * points at src with *disp_stride == src_stride.
 */
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

/* Same grey STE palette layout as startup Init_Greyscale_Palette. */
static void st_hw_palette_grey16(void)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	for (int i = 0; i < ST_HW_PAL_COUNT; i++) {
		unsigned short channel = (unsigned short)(((i >> 1) & 0x7) | ((i & 0x1) << 3));
		unsigned short color = (unsigned short)(((channel << 8) | (channel << 4) | channel));
		pr[i] = color;
	}
}

int st_run_interactive_gradient(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	st_hw_palette_read(saved_hw);

	unsigned char *chunky = (unsigned char *)malloc(320 * 200);
	unsigned char *planar = (unsigned char *)calloc(1, 32768);
	unsigned char pal[768];

	if (!chunky || !planar) {
		free(chunky);
		free(planar);
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		printf("  FAIL: allocation\n");
		return 1;
	}

	Setscreen(-1L, -1L, 0); /* low rez */

	memset(pal, 0, sizeof(pal));
	for (int i = 0; i < 256; i++)
		pal[i * 3 + 0] = pal[i * 3 + 1] = pal[i * 3 + 2] = (unsigned char)((i * 63) / 255);
	Set_Palette(pal);
	st_hw_palette_grey16();

	/* One black->white sweep per row (no x&255 wrap / repeat). */
	for (int y = 0; y < 200; y++)
		for (int x = 0; x < 320; x++)
			chunky[y * 320 + x] = (unsigned char)((x * 255) / 319);

	C2P_Render_Logical_To_ST_Screen(chunky, 320, planar);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();

	printf("\n=== INTERACTIVE: horizontal ramp ===\n");
	st_wrap_puts(
			"Expect one smooth grey ramp left to right "
			"(black to white), no band repeats on the right.",
			ST_TEXT_MAXCOL);

	int ok = st_read_yes_no();

	st_hw_palette_write(saved_hw);
	Setscreen(-1L, -1L, old_rez);
	Super(old_ssp);

	free(chunky);
	free(planar);
	return ok ? 0 : 1;
}

int st_run_interactive_htitle(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	unsigned char *raw_mix = NULL;
	size_t raw_len = 0;
	unsigned char *pcx = NULL;
	int w = 0, h = 0, stride = 0;
	unsigned char pal[768];
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	st_hw_palette_read(saved_hw);

	unsigned char *planar = (unsigned char *)calloc(1, 32768);
	if (!planar) {
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		printf("  FAIL: allocation\n");
		return 1;
	}

	int mx = st_mix_extract_file("UPDATE.MIX", "HTITLE.PCX", &raw_mix, &raw_len);
	if (mx != 0 || !raw_mix) {
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		free(planar);
		printf("  SKIP: UPDATE.MIX / HTITLE.PCX (mix err=%d)\n", mx);
		st_wrap_puts(
				"Place UPDATE.MIX in the current directory (same "
				"folder as the test .TOS).",
				ST_TEXT_MAXCOL);
		return 0;
	}

	int err = st_pcx_load_from_memory(raw_mix, raw_len, &pcx, &w, &h, &stride, pal);
	free(raw_mix);
	raw_mix = NULL;

	if (err != 0 || !pcx || w <= 0 || h <= 0) {
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		free(pcx);
		free(planar);
		printf("SKIP: HTITLE decode err=%d w=%d h=%d\n", err, w, h);
		return 0;
	}

	unsigned char *disp = NULL;
	int disp_stride = 320;
	unsigned char *down_free = NULL;
	if (st_prepare_320x200_chunky(pcx, w, h, stride, &disp, &disp_stride, &down_free) != 0 || !disp) {
		st_hw_palette_write(saved_hw);
		Super(old_ssp);
		free(pcx);
		free(planar);
		printf("SKIP: HTITLE downsample alloc failed (w=%d h=%d)\n", w, h);
		return 0;
	}

	Setscreen(-1L, -1L, 0);
	Set_Palette(pal);
	st_hw_palette_grey16();

	C2P_Render_Logical_To_ST_Screen(disp, disp_stride, planar);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();

	printf("\n=== INTERACTIVE: HTITLE (UPDATE.MIX) ===\n");
	st_wrap_puts(
			"HTITLE scaled to 320x200. Expect no half-width "
			"duplicate; title art readable.",
			ST_TEXT_MAXCOL);

	int ok = st_read_yes_no();

	st_hw_palette_write(saved_hw);
	Setscreen(-1L, -1L, old_rez);
	Super(old_ssp);

	free(down_free); /* only if we allocated a shrunk buffer */
	free(pcx);
	free(planar);
	return ok ? 0 : 1;
}
