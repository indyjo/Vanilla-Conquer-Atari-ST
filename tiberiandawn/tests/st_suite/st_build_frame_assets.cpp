/*
 * Interactive (test 7): pick a KeyFrame SHP from menu (1–9, 0=tenth), Build_Frame every frame,
 * tile them on a checker (CONQUER.MIX). Video preview has no printed text; Y/N is silent.
 *
 * Automated (test 8): Build_Frame spot-checks a fixed SHP list (includes XOR-heavy paths).
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

#include "misc.h" /* Build_Fading_Table */

#include "c2p.h"
#include "st_temperat_palette.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ST_HW_PAL_COUNT 16
#define ST_SCR_W 320
#define ST_SCR_H 200
/* Grid: 2px screen margin and 2px pad around each tile (compact, top-aligned). */
#define ST_BF7_PAD 2
/* Logical palette indices (TEMPERAT.PAL): 4x4 checker behind tiles (colors 3 and 4). */
#define ST_CHK_A ((unsigned char)3)
#define ST_CHK_B ((unsigned char)4)

/* Same layout as DisplayClass::UnitShadow / SHAPE_GHOST in KEYFBUFF.ASM (Single_Line_Ghost_Trans). */
#define ST_BF7_UNSHADOW_BYTES (((1) + 1) * 256) /* USHADOW_COL_COUNT + 1 */

/*
 * Build unit "ghost" tables: first 256 bytes = per-source index into blend bank (-1 = opaque);
 * bytes 256..511 = blend row 0 (LTGREEN -> BLACK fade), matching DISPLAY.CPP UShadowCols.
 */
static void st_build_unit_shadow_table(const unsigned char pal768[768], unsigned char *unit512)
{
	static TLucentType const ucols[1] = {
		{ (unsigned char)LTGREEN, (unsigned char)BLACK, 130, 0 },
	};

	memset(unit512, 0xFF, 256);
	unsigned char *table = unit512 + 256;
	for (int index = 0; index < 1; index++) {
		unit512[(int)ucols[index].SourceColor] = (unsigned char)index;
		Build_Fading_Table(pal768, table, ucols[index].DestColor, ucols[index].Fading);
		table += 256;
	}
}

/*
 * SHAPE_TRANS | SHAPE_GHOST style blit: skip src 0; remap opaque pixels; LTGREEN shadow blends
 * with destination (checker) like CC_Draw_Shape(..., SHAPE_GHOST, NULL, Map.UnitShadow).
 */
static void st_blit_tile_ghost(unsigned char *screen, int sx, int sy, int scr_stride,
		const unsigned char *src, int w, int h, int src_stride,
		const unsigned char *unit512, const unsigned char *remap)
{
	const unsigned char *is_trans = unit512;
	const unsigned char *blend_base = unit512 + 256;

	for (int yy = 0; yy < h; yy++) {
		if (sy + yy < 0 || sy + yy >= ST_SCR_H)
			continue;
		for (int xx = 0; xx < w; xx++) {
			if (sx + xx < 0 || sx + xx >= ST_SCR_W)
				continue;
			unsigned char s = src[yy * src_stride + xx];
			if (s == 0)
				continue;

			int row = (int)scr_stride * (sy + yy) + (sx + xx);
			unsigned char it = is_trans[s];
			if (it == 0xFFu) {
				screen[row] = remap ? remap[s] : s;
			} else {
				unsigned char d = screen[row];
				screen[row] = blend_base[((size_t)it << 8) + (size_t)d];
			}
		}
	}
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
static const unsigned short k_frames_gun[] = { 0, 1, 2, 3 };
static const unsigned short k_frames_sam[] = { 0, 1, 2, 3 };
static const unsigned short k_frames_minigun[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_fire1[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
/* OPTIONS.SHP frame 2 = OPTION_CONTROLS (game options dialog chrome); see GOPTIONS.CPP. */
static const unsigned short k_frames_options[] = { 2, 3 };
static const unsigned short k_frames_trex[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
/* ATOMSFX = nuclear blast (ADATA.CPP); sparse frames for autocheck. */
static const unsigned short k_frames_atomsfx[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20 };
static const unsigned short k_frames_a10[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16 };
static const unsigned short k_frames_silomake[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

static const StBfAsset k_assets[] = {
	{ "CONQUER.MIX", "E1.SHP", "infantry E1 (long XOR chain)", k_frames_e1,
		(int)(sizeof(k_frames_e1) / sizeof(k_frames_e1[0])) },
	{ "CONQUER.MIX", "E2.SHP", "infantry E2 (long XOR chain)", k_frames_e2,
		(int)(sizeof(k_frames_e2) / sizeof(k_frames_e2[0])) },
	{ "CONQUER.MIX", "GUN.SHP", "gun turret building", k_frames_gun,
		(int)(sizeof(k_frames_gun) / sizeof(k_frames_gun[0])) },
	{ "CONQUER.MIX", "SAM.SHP", "sam site building", k_frames_sam,
		(int)(sizeof(k_frames_sam) / sizeof(k_frames_sam[0])) },
	{ "CONQUER.MIX", "MINIGUN.SHP", "MINIGUN muzzle", k_frames_minigun,
		(int)(sizeof(k_frames_minigun) / sizeof(k_frames_minigun[0])) },
	{ "CONQUER.MIX", "FIRE1.SHP", "FIRE1 ground fire", k_frames_fire1,
		(int)(sizeof(k_frames_fire1) / sizeof(k_frames_fire1[0])) },
	{ "CONQUER.MIX", "OPTIONS.SHP", "options dialog bits", k_frames_options,
		(int)(sizeof(k_frames_options) / sizeof(k_frames_options[0])) },
	{ "CONQUER.MIX", "TREX.SHP", "T. rex unit", k_frames_trex,
		(int)(sizeof(k_frames_trex) / sizeof(k_frames_trex[0])) },
	{ "CONQUER.MIX", "ATOMSFX.SHP", "nuclear blast FX", k_frames_atomsfx,
		(int)(sizeof(k_frames_atomsfx) / sizeof(k_frames_atomsfx[0])) },
	{ "CONQUER.MIX", "A10.SHP", "A-10 Warthog", k_frames_a10,
		(int)(sizeof(k_frames_a10) / sizeof(k_frames_a10[0])) },
	{ "CONQUER.MIX", "SILOMAKE.SHP", "ore silo build anim", k_frames_silomake,
		(int)(sizeof(k_frames_silomake) / sizeof(k_frames_silomake[0])) },
};

static void st_fill_checkerboard_4x4(unsigned char *screen, int scr_w, int scr_h, int scr_stride)
{
	for (int y = 0; y < scr_h; y++) {
		for (int x = 0; x < scr_w; x++) {
			int cell = ((x >> 2) ^ (y >> 2)) & 1;
			screen[y * scr_stride + x] = cell ? ST_CHK_B : ST_CHK_A;
		}
	}
}

/*
 * Test 7: pick entry in k_assets[] (console only, before video preview).
 * Keys 1-9 and 0 match array order for the first 10 slots; 's' selects SAM.
 * Other keys default to 0 (E1).
 */
static int st_read_build7_shape_choice(void)
{
	printf(
			"\n"
			"1=E1 2=E2 3=GUN 4=SAM 5=MINIGUN 6=FIRE1 7=OPTIONS 8=TREX\n"
			"9=ATOMSFX 0=A10 s=SILOMAKE\n"
			"Choice: ");
	fflush(stdout);
	long w = Crawcin();
	unsigned char ch = (unsigned char)(w & 0xFF);
	printf("%c\n", ch ? ch : '?');
	if (ch >= '1' && ch <= '9')
		return (int)(ch - '1');
	if (ch == '0')
		return 9;
	if (ch == 's' || ch == 'S')
		return 10;
	return 0;
}

static int st_build7_frames_per_page(unsigned short tw, unsigned short th)
{
	const int cell_w = (int)tw + ST_BF7_PAD * 2;
	const int cell_h = (int)th + ST_BF7_PAD * 2;
	int cols = ((int)ST_SCR_W - ST_BF7_PAD * 2) / cell_w;
	if (cols < 1)
		cols = 1;
	int rows = ((int)ST_SCR_H - ST_BF7_PAD * 2) / cell_h;
	if (rows < 1)
		rows = 1;
	return cols * rows;
}

static int st_build7_next_remap(int remap_index)
{
	int next = remap_index + 1;
	if (next >= 6) {
		next = 0;
	}
	return next;
}

static const unsigned char *st_build7_remap_table(int remap_index)
{
	static unsigned char const * const k_remaps[] = {
		RemapGold,
		RemapRed,
		RemapLtBlue,
		RemapOrange,
		RemapGreen,
		RemapBlue
	};

	if (remap_index < 0 || remap_index >= (int)(sizeof(k_remaps) / sizeof(k_remaps[0]))) {
		return NULL;
	}
	return k_remaps[remap_index];
}

static const char *st_build7_remap_name(int remap_index)
{
	static const char * const k_names[] = {
		"GOLD",
		"RED",
		"LTBLUE",
		"ORANGE",
		"GREEN",
		"BLUE"
	};

	if (remap_index < 0 || remap_index >= (int)(sizeof(k_names) / sizeof(k_names[0]))) {
		return "UNKNOWN";
	}
	return k_names[remap_index];
}

static void st_build7_status_line(int remap_index, int page_index, int page_count)
{
	/*
	 * VT52 direct cursor address:
	 *   ESC Y row+32 col+32
	 * ST low-res text console is 40x25, so row 24 is the bottom line.
	 */
	printf("\033Y%c%c", (char)(24 + 32), (char)(0 + 32));
	printf("R:%s P:%d/%d SPC nxt H remap Y ok", st_build7_remap_name(remap_index), page_index + 1, page_count);
	fflush(stdout);
}

/*
 * Tile frames [start_frame, end_frame) on chunky; top-aligned, ST_BF7_PAD margin and pad.
 * Skips frames where Build_Frame fails (slot stays checker).
 */
static void st_blit_frame_page_on_checker(unsigned char *chunky, void *raw, size_t raw_len,
		unsigned short tw, unsigned short th, unsigned short start_frame, unsigned short end_frame,
		const unsigned char *remap)
{
	if (!chunky || !raw || tw == 0 || th == 0 || start_frame >= end_frame)
		return;

	const int cell_w = (int)tw + ST_BF7_PAD * 2;
	const int cell_h = (int)th + ST_BF7_PAD * 2;
	int cols = ((int)ST_SCR_W - ST_BF7_PAD * 2) / cell_w;
	if (cols < 1)
		cols = 1;
	const int cy0 = ST_BF7_PAD;

	unsigned long need = Get_Build_Frame_BufferBytes(raw);
	if (need == 0)
		return;
	unsigned char *buf = (unsigned char *)malloc((size_t)need);
	if (!buf)
		return;

	unsigned char unit_shadow[ST_BF7_UNSHADOW_BYTES];
	st_build_unit_shadow_table(kStTemperatPal768, unit_shadow);

	for (unsigned short fr = start_frame; fr < end_frame; fr++) {
		int slot = (int)(fr - start_frame);
		int col = slot % cols;
		int row = slot / cols;
		int cx = ST_BF7_PAD + col * cell_w;
		int cy = cy0 + row * cell_h;

		memset(buf, 0, (size_t)need);
		unsigned long br = Build_Frame(raw, fr, buf, raw_len);
		if (!br)
			continue;

		st_blit_tile_ghost(chunky, cx, cy, ST_SCR_W, buf, tw, th, tw, unit_shadow, remap);
	}

	free(buf);
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
		unsigned long need = Get_Build_Frame_BufferBytes(raw);
		if (need == 0) {
			printf("SKIP %s:%s bad buffer size\n", a->mix, a->shp);
			free(raw);
			continue;
		}
		unsigned char *buf = (unsigned char *)malloc((size_t)need);
		if (!buf) {
			free(raw);
			fails++;
			continue;
		}
		for (int fi = 0; fi < a->nframes; fi++) {
			unsigned short fr = a->frames[fi];
			if (fr >= tc)
				continue;
			memset(buf, 0, (size_t)need);
			unsigned long r = Build_Frame(raw, fr, buf, raw_len);
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
	long old_phys = (long)Physbase();
	long old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);

	unsigned char *chunky = (unsigned char *)calloc(1, (size_t)ST_SCR_W * ST_SCR_H);
	if (!chunky) {
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		printf("FAIL: oom chunky\n");
		return 1;
	}

	const int pick = st_read_build7_shape_choice();
	const size_t n_menu = sizeof(k_assets) / sizeof(k_assets[0]);
	const StBfAsset *sel = &k_assets[(pick >= 0 && (size_t)pick < n_menu) ? (size_t)pick : 0u];

	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int mx = st_mix_extract_file(sel->mix, sel->shp, &raw, &raw_len);
	if (mx != 0 || !raw) {
		printf("SKIP %s:%s err=%d (%s)\n", sel->mix, sel->shp, mx, sel->desc);
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		return 1;
	}

	unsigned short tw = Get_Build_Frame_Width(raw);
	unsigned short th = Get_Build_Frame_Height(raw);
	unsigned short tc = Get_Build_Frame_Count(raw);
	if (tw == 0 || th == 0 || tc == 0) {
		printf("SKIP %s:%s bad header\n", sel->mix, sel->shp);
		free(raw);
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		return 1;
	}

	{
		unsigned char pal[768];
		memcpy(pal, kStTemperatPal768, 768);
		Set_Palette(pal);
	}

	Setscreen(-1L, -1L, 0);
	unsigned char *planar = (unsigned char *)Logbase();
	if (!planar) {
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		Super(old_ssp);
		return 1;
	}
	St_HW_Palette_Write_Temperat_First16(ST_HW_PALETTE_REGS);
	C2P_Render_Logical_To_ST_Screen(chunky, ST_SCR_W, planar);
	Setscreen((long)planar, (long)planar, -1L);
	Vsync();
	Vsync();

	int ok = 1;
	int frames_per_page = st_build7_frames_per_page(tw, th);
	if (frames_per_page < 1)
		frames_per_page = 1;
	unsigned short page_start = 0;
	int remap_index = 0;
	for (;;) {
		unsigned short page_end = (unsigned short)MIN((int)tc, (int)page_start + frames_per_page);
		const unsigned char *remap = st_build7_remap_table(remap_index);
		int page_index = page_start / frames_per_page;
		int page_count = (tc + frames_per_page - 1) / frames_per_page;
		st_fill_checkerboard_4x4(chunky, ST_SCR_W, ST_SCR_H, ST_SCR_W);
		st_blit_frame_page_on_checker(chunky, raw, raw_len, tw, th, page_start, page_end, remap);
		C2P_Render_Logical_To_ST_Screen(chunky, ST_SCR_W, planar);
		Setscreen((long)planar, (long)planar, -1L);
		Vsync();
		Vsync();
		st_build7_status_line(remap_index, page_index, page_count);

		long w = Crawcin();
		unsigned char ch = (unsigned char)(w & 0xFF);
		if (ch == 'h' || ch == 'H') {
			remap_index = st_build7_next_remap(remap_index);
			continue;
		}
		if (ch == ' ' && page_end < tc) {
			page_start = page_end;
			continue;
		}

		ok = (ch == 'y' || ch == 'Y' || ch == ' ');
		break;
	}

	free(raw);
	free(chunky);

	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	Super(old_ssp);
	return ok ? 0 : 1;
}
