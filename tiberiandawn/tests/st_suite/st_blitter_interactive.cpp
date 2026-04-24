/*
 * Automated: TITLE.CPS — per direction & increment (1,2,3): HW restore from offscreen,
 * then 16-step smooth scroll via hardware blit (no keypresses).
 * Validates: after each round, planar nibble at screen center vs hidden origin pixel.
 */

#include "function.h"
#include "c2p.h"
#include "palette.h"
#include "st_blitter_blit.h"
#include "st_mix_minimal.h"
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

	Setscreen(-1L, -1L, 0);
	unsigned char *planar = (unsigned char *)Logbase();
	if (!planar) {
		free(hidden);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  FAIL: no Logbase\n");
		return 1;
	}

	C2P_Select_WeightSet(C2P_WEIGHTSET_HTITLE);
	GraphicBufferClass screen;
	screen.Init(320, 200, planar, 32768L, (int)GBC_ST_PLANAR_LORES);
	GraphicViewPortClass vp(&screen, 0, 0, 320, 200);
	vp.Clear(0);
	memset(CurrentPalette, 0x01, 768);
	{
		unsigned char warm[768];
		memcpy(warm, kStTemperatPal768, 768);
		Set_Palette(warm);
	}
	Load_Title_Screen((char *)"TITLE.CPS", &vp, pal);
	Set_Palette(pal);
	St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal);
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
		printf("  Test 9 (TITLE blitter scroll): FAIL\n");
	else
		printf("  Test 9 (TITLE blitter scroll): PASS\n");

	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	Super(old_ssp);

	free(hidden);
	return any_fail ? 1 : 0;
}

static int st_try_load_temperate_template_tile(unsigned char *dst24x24)
{
	static const char *kTemplates[] = {
		"CLEAR1.TEM",
		"D01.TEM",
		"D02.TEM",
		"SH1.TEM",
		"RV01.TEM",
		"TI1.TEM",
	};
	printf(
		"\n"
		"Template selection (TEMPERAT.MIX):\n"
		"  1) CLEAR1.TEM\n"
		"  2) D01.TEM\n"
		"  3) D02.TEM\n"
		"  4) SH1.TEM\n"
		"  5) RV01.TEM\n"
		"  6) TI1.TEM\n"
		"Choice: ");
	long wch = Crawcin();
	unsigned char ch = (unsigned char)(wch & 0xFF);
	printf("%c\n", ch ? ch : '?');
	const int pick = (ch >= '1' && ch <= '6') ? (int)(ch - '1') : 0;

	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int mx = st_mix_extract_file("TEMPERAT.MIX", kTemplates[pick], &raw, &raw_len);
	if (mx != 0 || !raw) {
		printf("  WARN: cannot load %s from TEMPERAT.MIX (mix err=%d)\n", kTemplates[pick], mx);
		return 0;
	}

	/* TEM entries are iconset blobs (little-endian offsets), not Build_Frame blocks. */
	auto rd16 = [](const unsigned char *p) -> int {
		return (int)((unsigned)p[0] | ((unsigned)p[1] << 8));
	};
	auto rd32 = [](const unsigned char *p) -> unsigned long {
		return (unsigned long)p[0] |
			((unsigned long)p[1] << 8) |
			((unsigned long)p[2] << 16) |
			((unsigned long)p[3] << 24);
	};
	const unsigned char *base = raw;
	const int iw = rd16(base + 0);
	const int ih = rd16(base + 2);
	const int icount = rd16(base + 4);
	const unsigned long icons_off = rd32(base + 12);
	const unsigned long map_off = rd32(base + 28);
	const int icon_size = iw * ih;
	if (iw <= 0 || ih <= 0 || iw > 128 || ih > 128 || icount <= 0 || icon_size <= 0
		|| icons_off == 0 || (size_t)icons_off >= raw_len) {
		free(raw);
		printf("  WARN: invalid template header for %s\n", kTemplates[pick]);
		return 0;
	}
	if ((size_t)icons_off + (size_t)icon_size * (size_t)icount > raw_len) {
		free(raw);
		printf("  WARN: icon payload out of range for %s\n", kTemplates[pick]);
		return 0;
	}

	unsigned char *framebuf = (unsigned char *)malloc((size_t)icon_size);
	uint8_t *preview = (uint8_t *)malloc(32000u);
	if (!framebuf || !preview) {
		free(framebuf);
		free(preview);
		free(raw);
		printf("  WARN: allocation failed for template preview\n");
		return 0;
	}

	const int cols = 5;
	const int rows = 2;
	const int per_page = cols * rows;
	const int page_count = (icount + per_page - 1) / per_page;
	int page = 0;
	int selected_icon = 0;
	int done = 0;

	while (!done) {
		for (int y = 0; y < ST_PLANAR_HEIGHT; y++) {
			for (int x = 0; x < ST_PLANAR_WIDTH; x++) {
				const unsigned char bg = (((x >> 4) ^ (y >> 4)) & 1) ? 2 : 4;
				ST_Planar_PutPixel(preview, x, y, bg);
			}
		}

		const int start = page * per_page;
		const int shown = ((start + per_page) <= icount) ? per_page : (icount - start);
		const int cell_w = 64;
		const int cell_h = 90;
		for (int i = 0; i < shown; i++) {
			const int logical_icon = start + i;
			int icon_index = logical_icon;
			if (map_off != 0 && (size_t)map_off + (size_t)logical_icon < raw_len) {
				icon_index = (int)base[map_off + logical_icon];
			}
			if (icon_index < 0 || icon_index >= icount)
				continue;
			memcpy(framebuf, base + icons_off + (size_t)icon_index * (size_t)icon_size, (size_t)icon_size);
			const int col = i % cols;
			const int row = i / cols;
			const int dx = col * cell_w + 8;
			const int dy = row * cell_h + 8;
			C2P_Blit_Linear8_To_Planar(preview, dx, dy, framebuf, iw, ih, iw, 0);
		}

		Setscreen((long)preview, (long)preview, -1L);
		Vsync();
		printf("  %s icons=%d size=%dx%d page=%d/%d\n",
			kTemplates[pick], icount, iw, ih, page + 1, page_count);
		printf("  Select slot 0..9 on this page, n=next, p=prev, q=cancel: ");
		long kw = Crawcin();
		unsigned char kc = (unsigned char)(kw & 0xFF);
		printf("%c\n", kc ? kc : '?');

		if (kc == 'n' || kc == 'N') {
			if (page + 1 < page_count)
				page++;
			continue;
		}
		if (kc == 'p' || kc == 'P') {
			if (page > 0)
				page--;
			continue;
		}
		if (kc == 'q' || kc == 'Q') {
			free(preview);
			free(framebuf);
			free(raw);
			return 0;
		}
		if (kc >= '0' && kc <= '9') {
			const int slot = (int)(kc - '0');
			if (slot < shown) {
				selected_icon = start + slot;
				done = 1;
			}
		}
	}

	int icon_index = selected_icon;
	if (map_off != 0 && (size_t)map_off + (size_t)selected_icon < raw_len) {
		icon_index = (int)base[map_off + selected_icon];
	}
	if (icon_index < 0 || icon_index >= icount) {
		free(preview);
		free(framebuf);
		free(raw);
		printf("  WARN: selected icon out of range (%d)\n", icon_index);
		return 0;
	}
	memcpy(framebuf, base + icons_off + (size_t)icon_index * (size_t)icon_size, (size_t)icon_size);

	memset(dst24x24, 0, 24u * 24u);
	const int copy_w = (iw < 24) ? iw : 24;
	const int copy_h = (ih < 24) ? ih : 24;
	for (int y = 0; y < copy_h; y++) {
		memcpy(dst24x24 + (y * 24), framebuf + (y * iw), (size_t)copy_w);
	}

	printf("  Using %s icon=%d(raw=%d) (copied %dx%d into 24x24)\n",
		kTemplates[pick], selected_icon, icon_index, copy_w, copy_h);

	free(preview);
	free(framebuf);
	free(raw);
	return 1;
}

static void st_planar_copy_ref(
	uint8_t *dst,
	int dx,
	int dy,
	const uint8_t *src,
	int src_row_bytes,
	int src_width,
	int src_height,
	int sx,
	int sy,
	int w,
	int h)
{
	auto src_get_pixel = [](const uint8_t *base, int row_bytes, int width_px, int height_px, int x, int y) -> unsigned char {
		if (!base || row_bytes <= 0 || x < 0 || y < 0 || x >= width_px || y >= height_px)
			return 0;
		const uint8_t *p = base + (size_t)y * (size_t)row_bytes + (size_t)((x >> 4) * 8) + (size_t)((x >> 3) & 1);
		const int bitnum = 7 - (x & 7);
		const uint8_t mask = (uint8_t)(1u << bitnum);
		unsigned char c = 0;
		for (int pl = 0; pl < 4; pl++) {
			if (p[pl * 2] & mask)
				c |= (unsigned char)(1u << pl);
		}
		return c;
	};

	for (int yy = 0; yy < h; yy++) {
		for (int xx = 0; xx < w; xx++) {
			const unsigned char c = src_get_pixel(src, src_row_bytes, src_width, src_height, sx + xx, sy + yy);
			ST_Planar_PutPixel(dst, dx + xx, dy + yy, c);
		}
	}
}

/*
 * Test 10:
 * Fixed pipeline validation for one 24x24 "cell" across 23x23 destination offsets.
 *   linear scratch -> aligned planar scratch -> blitter to arbitrary dx/dy
 * Compares hardware-path result against software reference C2P_Blit_Linear8_To_Planar.
 */
int st_run_blitter_tile_skew_matrix(void)
{
	long old_ssp = Super(0L);
	int old_rez = Getrez();
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	st_hw_palette_read(saved_hw);
	const int old_weight_set = C2P_Get_WeightSet();

	Setscreen(-1L, -1L, 0); /* 320x200x16 */
	uint8_t *screen_hw = (uint8_t *)Logbase();
	uint8_t *screen_ref = (uint8_t *)malloc(32000u);
	uint8_t *planar_scratch = (uint8_t *)malloc((64u / 16u) * 8u * 24u);
	uint8_t tile_linear[24 * 24];
	if (!screen_hw || !screen_ref || !planar_scratch) {
		free(screen_ref);
		free(planar_scratch);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  FAIL: allocation\n");
		return 1;
	}

	/* Use same TEMPERAT palette path as other ST tests. */
	{
		unsigned char pal[768];
		memcpy(pal, kStTemperatPal768, sizeof(pal));
		Set_Palette(pal);
		St_HW_Palette_Write_Temperat_First16(ST_HW_PALETTE_REGS);
	}
	C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);

	if (!st_try_load_temperate_template_tile(tile_linear)) {
		free(screen_ref);
		free(planar_scratch);
		C2P_Select_WeightSet(old_weight_set);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("  FAIL: no template/frame selected\n");
		return 1;
	}
	int mismatches = 0;
	const int tile_w = 24;
	const int tile_h = 24;
	const int max_skew = 22; /* ox/oy sweep 0..22 */
	/* Reserve room per slot for tile + worst-case skew so no OOB blits occur. */
	const int cell_w = tile_w + max_skew + 2; /* 48 */
	const int cell_h = tile_h + max_skew + 2; /* 48 */
	const int cols = ST_PLANAR_WIDTH / cell_w;   /* 6 */
	const int rows = ST_PLANAR_HEIGHT / cell_h;  /* 4 */
	const int per_page = cols * rows;
	const int total_cases = 23 * 23;
	const int page_count = (total_cases + per_page - 1) / per_page;
	int case_index = 0;

	for (int page = 0; page < page_count; page++) {
		/* Draw background once per page. */
		for (int y = 0; y < ST_PLANAR_HEIGHT; y++) {
			for (int x = 0; x < ST_PLANAR_WIDTH; x++) {
				const unsigned char bg = (((x >> 4) ^ (y >> 4)) & 1) ? 2 : 4;
				ST_Planar_PutPixel(screen_hw, x, y, bg);
				ST_Planar_PutPixel(screen_ref, x, y, bg);
			}
		}

		for (int slot = 0; slot < per_page && case_index < total_cases; slot++, case_index++) {
			const int ox = case_index % 23;
			const int oy = case_index / 23;
			const int col = slot % cols;
			const int row = slot / cols;
			/* Row/column arrangement; each slot shows one (ox,oy) skew variation. */
			const int dx = col * cell_w + ox;
			const int dy = row * cell_h + oy;
			const int sx = dx & 15;

			memset(planar_scratch, 0, (size_t)((64 / 16) * 8 * 24));
			C2P_Render_Logical_To_Planar_Rect(
				tile_linear, tile_w, tile_h, tile_w,
				planar_scratch, (64 / 16) * 8, 64, 24,
				sx, 0, 0, 0);

			if (!ST_Blitter_Planar_Rect_Blit(
					planar_scratch,
					(64 / 16) * 8,
					64,
					24,
					sx,
					0,
					screen_hw,
					ST_PLANAR_BYTES_PER_LINE,
					ST_PLANAR_WIDTH,
					ST_PLANAR_HEIGHT,
					dx,
					dy,
					tile_w,
					tile_h)) {
				printf("  FAIL blit call case=%d ox=%d oy=%d dx=%d dy=%d sx=%d\n",
					case_index, ox, oy, dx, dy, sx);
				mismatches++;
				continue;
			}

			/*
			 * Reference copy for this pipeline:
			 * copy already-converted planar pixels from scratch to destination.
			 * (Do NOT re-run C2P_Blit_Linear8_To_Planar here; that uses screen-absolute
			 * dither phase and is a different model than the scratch->blit pipeline.)
			 */
			st_planar_copy_ref(screen_ref, dx, dy, planar_scratch, (64 / 16) * 8, 64, 24, sx, 0, tile_w, tile_h);

			if (memcmp(screen_hw, screen_ref, 32000u) != 0) {
				if (mismatches < 20) {
					printf("  MISMATCH case=%d ox=%d oy=%d dx=%d dy=%d sx=%d\n",
						case_index, ox, oy, dx, dy, sx);
				}
				mismatches++;
			}
		}

		Setscreen((long)screen_hw, (long)screen_hw, -1L);
		Vsync();
		printf("  Test10 page %d/%d shown. Press any key...\n", page + 1, page_count);
		(void)Crawcin();
	}

	printf("  Test 10 (tile skew matrix 23x23): %s (mismatches=%d)\n",
		(mismatches == 0) ? "PASS" : "FAIL",
		mismatches);

	/* Last iteration remains visible on-screen. */
	Setscreen((long)screen_hw, (long)screen_hw, -1L);
	Vsync();

	free(screen_ref);
	free(planar_scratch);
	C2P_Select_WeightSet(old_weight_set);
	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	Super(old_ssp);
	return (mismatches == 0) ? 0 : 1;
}
