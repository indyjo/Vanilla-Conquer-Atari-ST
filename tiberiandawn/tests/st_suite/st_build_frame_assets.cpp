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

#include "fading.h"

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

typedef struct {
	StBfAsset asset;
	char shp_name[32];
} StBfMenuAsset;

static int st_extract_asset_preferring_dos_conquer(const char *mix, const char *entry_name,
		unsigned char **out_data, size_t *out_size)
{
	if (mix && entry_name && strcmp(mix, "CONQUER.MIX") == 0) {
		int rc = st_mix_extract_file("dos/CONQUER.MIX", entry_name, out_data, out_size);
		if (rc == 0) {
			return 0;
		}
	}
	return st_mix_extract_file(mix, entry_name, out_data, out_size);
}

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
static const unsigned short k_frames_power[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_radar_gdi[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_pips[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_dot_shp[] = { 0 };
static const unsigned short k_frames_bar3ylw[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_bar3red[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_frames_icon[] = { 0 };

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
	{ "CONQUER.MIX", "POWER.SHP", "power bar marker", k_frames_power,
		(int)(sizeof(k_frames_power) / sizeof(k_frames_power[0])) },
	{ "CONQUER.MIX", "RADAR.GDI", "GDI radar logo", k_frames_radar_gdi,
		(int)(sizeof(k_frames_radar_gdi) / sizeof(k_frames_radar_gdi[0])) },
	{ "CONQUER.MIX", "PIPS.SHP", "pip markers", k_frames_pips,
		(int)(sizeof(k_frames_pips) / sizeof(k_frames_pips[0])) },
	{ "CONQUER.MIX", ".SHP", "literal .SHP entry", k_frames_dot_shp,
		(int)(sizeof(k_frames_dot_shp) / sizeof(k_frames_dot_shp[0])) },
	{ "CONQUER.MIX", "BAR3YLW.SHP", "yellow bar frames", k_frames_bar3ylw,
		(int)(sizeof(k_frames_bar3ylw) / sizeof(k_frames_bar3ylw[0])) },
	{ "CONQUER.MIX", "BAR3RED.SHP", "red bar frames", k_frames_bar3red,
		(int)(sizeof(k_frames_bar3red) / sizeof(k_frames_bar3red[0])) },

	/* All *ICON.SHP entries from mix2.txt, sorted alphabetically. */
	{ "CONQUER.MIX", "A10ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "AFLDICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "APCICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "ARCOICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "ARTYICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "ATOMICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "ATWRICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BARBICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BGGYICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BIKEICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BIOICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BOATICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BOMBICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "BRIKICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "C17ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "CYCLICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "E1ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "E2ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "E3ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "E4ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "E5ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "E6ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "EYEICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "FACTICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "FIXICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "FTNKICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "GTWRICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "GUNICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HANDICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HARVICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HELIICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HOSPICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HQICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HPADICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "HTNKICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "IONICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "JEEPICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "LSTICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "LTNKICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "MCVICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "MHQICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "MLRSICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "MSAMICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "MTNKICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "NUK2ICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "NUKEICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "OBLIICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "ORCAICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "PROCICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "PUMPICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "PYLEICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "ROADICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "RMBOICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "SAMICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "SBAGICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "SILOICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "STNKICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "TMPLICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "TRANICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "WEAPICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
	{ "CONQUER.MIX", "WOODICON.SHP", "icon asset", k_frames_icon,
		(int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0])) },
};

static const char * const k_mix2_shps[] = {
	"120MM.SHP","50CAL.SHP","A10.SHP","A10ICON.SHP","AFLD.SHP","AFLDICON.SHP","AFLDMAKE.SHP","APC.SHP","APCICON.SHP","ARCO.SHP",
	"ARCOICON.SHP","ART-EXP1.SHP","ARTY.SHP","ARTYICON.SHP","ATOMDOOR.SHP","ATOMICDN.SHP","ATOMICON.SHP","ATOMICUP.SHP","ATOMSFX.SHP","ATWR.SHP",
	"ATWRICON.SHP","ATWRMAKE.SHP","BAR3RED.SHP","BAR3YLW.SHP","BARB.SHP","BARBICON.SHP","BGGY.SHP","BGGYICON.SHP","BIKE.SHP","BIKEICON.SHP",
	"BIO.SHP","BIOICON.SHP","BIOMAKE.SHP","BOAT.SHP","BOATICON.SHP","BOMB.SHP","BOMBICON.SHP","BOMBLET.SHP","BRIK.SHP","BRIKICON.SHP",
	"BTN-DN.SHP","BTN-PL.SHP","BTN-ST.SHP","BTN-UP.SHP","BURN-L.SHP","BURN-M.SHP","BURN-S.SHP","C1.SHP","C10.SHP","C17.SHP",
	"C17ICON.SHP","C2.SHP","C3.SHP","C4.SHP","C5.SHP","C6.SHP","C7.SHP","C8.SHP","C9.SHP","CHAN.SHP",
	"CHEM-E.SHP","CHEM-N.SHP","CHEM-NE.SHP","CHEM-NW.SHP","CHEM-S.SHP","CHEM-SE.SHP","CHEM-SW.SHP","CHEM-W.SHP","CHEMBALL.SHP","CLOCK.SHP",
	"CONC.SHP","COUNTRYA.SHP","COUNTRYE.SHP","CREDS.SHP","CYCL.SHP","CYCLICON.SHP","DELPHI.SHP","DEVIATOR.SHP","DOLLAR.SHP","DRAGON.SHP",
	"E1.SHP","E1ICON.SHP","E1ROT.SHP","E2.SHP","E2ICON.SHP","E2ROT.SHP","E3.SHP","E3ICON.SHP","E3ROT.SHP","E4.SHP",
	"E4ICON.SHP","E4ROT.SHP","E5.SHP","E5ICON.SHP","E6.SHP","E6ICON.SHP","EARTH.SHP","EMPULSE.SHP","EYE.SHP","EYEICON.SHP",
	"EYEMAKE.SHP","FACT.SHP","FACTICON.SHP","FACTMAKE.SHP","FBALL1.SHP","FIRE1.SHP","FIRE2.SHP","FIRE3.SHP","FIRE4.SHP","FIX.SHP",
	"FIXICON.SHP","FIXMAKE.SHP","FLAGFLY.SHP","FLAME-E.SHP","FLAME-N.SHP","FLAME-NE.SHP","FLAME-NW.SHP","FLAME-S.SHP","FLAME-SE.SHP","FLAME-SW.SHP",
	"FLAME-W.SHP","FLMSPT.SHP","FPLS.SHP","FRAG1.SHP","FRAG3.SHP","FTNK.SHP","FTNKICON.SHP","GTWR.SHP","GTWRICON.SHP","GTWRMAKE.SHP",
	"GUN.SHP","GUNFIRE.SHP","GUNICON.SHP","GUNMAKE.SHP","HAND.SHP","HANDICON.SHP","HANDMAKE.SHP","HARV.SHP","HARVICON.SHP","HELI.SHP",
	"HELIICON.SHP","HISCORE1.SHP","HISCORE2.SHP","HOSP.SHP","HOSPICON.SHP","HOSPMAKE.SHP","HPAD.SHP","HPADICON.SHP","HPADMAKE.SHP","HQ.SHP",
	"HQICON.SHP","HQMAKE.SHP","HTNK.SHP","HTNKICON.SHP","INVUN.SHP","IONICON.SHP","IONSFX.SHP","JEEP.SHP","JEEPICON.SHP","LOGOS.SHP",
	"LROTOR.SHP","LST.SHP","LSTICON.SHP","LTNK.SHP","LTNKICON.SHP","MCV.SHP","MCVICON.SHP","MHQ.SHP","MHQICON.SHP","MINE.SHP",
	"MINIGUN.SHP","MISS.SHP","MISSILE.SHP","MISSILE2.SHP","MLRS.SHP","MLRSICON.SHP","MOEBIUS.SHP","MOUSE.SHP","MOVEFLSH.SHP","MSAM.SHP",
	"MSAMICON.SHP","MTNK.SHP","MTNKICON.SHP","NAPALM1.SHP","NAPALM2.SHP","NAPALM3.SHP","NUK2.SHP","NUK2ICON.SHP","NUK2MAKE.SHP","NUKE.SHP",
	"NUKEICON.SHP","NUKEMAKE.SHP","OBLI.SHP","OBLIICON.SHP","OBLIMAKE.SHP","OPTIONS.SHP","ORCA.SHP","ORCAICON.SHP","PATRIOT.SHP","PIFF.SHP",
	"PIFFPIFF.SHP","PIPS.SHP","POWER.SHP","PROC.SHP","PROCICON.SHP","PROCMAKE.SHP","PUMPICON.SHP","PUMPMAKE.SHP","PYLE.SHP","PYLEICON.SHP",
	"PYLEMAKE.SHP","RAPID.SHP","RAPT.SHP","RMBO.SHP","RMBOICON.SHP","ROAD.SHP","ROADICON.SHP","RROTOR.SHP","SAM.SHP","SAMFIRE.SHP",
	"SAMICON.SHP","SAMMAKE.SHP","SBAG.SHP","SBAGICON.SHP","SCRATE.SHP","SELECT.SHP","SHADOW.SHP","SILO.SHP","SILOICON.SHP","SILOMAKE.SHP",
	"SMOKEY.SHP","SMOKE_M.SHP","SMOKLAND.SHP","SQUISH.SHP","STEALTH2.SHP","STEG.SHP","STNK.SHP","STNKICON.SHP","STRIP.SHP","STRIPDN.SHP",
	"STRIPUP.SHP","TABS.SHP","TIME.SHP","TMPL.SHP","TMPLICON.SHP","TMPLMAKE.SHP","TRAN.SHP","TRANICON.SHP","TREX.SHP","TRIC.SHP",
	"V19.SHP","VEH-HIT1.SHP","VEH-HIT2.SHP","VEH-HIT3.SHP","VICE.SHP","WAKE.SHP","WCRATE.SHP","WEAP.SHP","WEAP2.SHP","WEAPICON.SHP",
	"WEAPMAKE.SHP","WOOD.SHP","WOODICON.SHP"
};

static int st_ascii_upper(int c)
{
	if (c >= 'a' && c <= 'z') {
		return c - ('a' - 'A');
	}
	return c;
}

static int st_casecmp(const char *a, const char *b)
{
	while (*a && *b) {
		int da = st_ascii_upper((unsigned char)*a++);
		int db = st_ascii_upper((unsigned char)*b++);
		if (da != db) {
			return da - db;
		}
	}
	return st_ascii_upper((unsigned char)*a) - st_ascii_upper((unsigned char)*b);
}

static int st_has_shp_ext(const char *name)
{
	size_t n = strlen(name);
	return n >= 4 &&
		st_ascii_upper((unsigned char)name[n - 4]) == '.' &&
		st_ascii_upper((unsigned char)name[n - 3]) == 'S' &&
		st_ascii_upper((unsigned char)name[n - 2]) == 'H' &&
		st_ascii_upper((unsigned char)name[n - 1]) == 'P';
}

static int st_menu_find_name(const StBfMenuAsset *items, int count, const char *name)
{
	for (int i = 0; i < count; i++) {
		if (st_casecmp(items[i].asset.shp, name) == 0) {
			return i;
		}
	}
	return -1;
}

static int st_menu_cmp_asset_name(const void *a, const void *b)
{
	const StBfMenuAsset *aa = (const StBfMenuAsset *)a;
	const StBfMenuAsset *bb = (const StBfMenuAsset *)b;
	return st_casecmp(aa->shp_name, bb->shp_name);
}

static int st_build7_collect_assets(StBfMenuAsset *items, int cap)
{
	if (!items || cap <= 0) {
		return 0;
	}

	int count = 0;
	for (int i = 0; i < (int)(sizeof(k_assets) / sizeof(k_assets[0])) && count < cap; i++) {
		/* Keep only .SHP in the interactive menu. */
		if (!st_has_shp_ext(k_assets[i].shp)) {
			continue;
		}
		strncpy(items[count].shp_name, k_assets[i].shp, sizeof(items[count].shp_name) - 1);
		items[count].shp_name[sizeof(items[count].shp_name) - 1] = '\0';
		items[count].asset = k_assets[i];
		items[count].asset.shp = items[count].shp_name;
		count++;
	}

	for (int i = 0; i < (int)(sizeof(k_mix2_shps) / sizeof(k_mix2_shps[0])) && count < cap; i++) {
		const char *token = k_mix2_shps[i];
		if (st_menu_find_name(items, count, token) >= 0) {
			continue;
		}
		strncpy(items[count].shp_name, token, sizeof(items[count].shp_name) - 1);
		items[count].shp_name[sizeof(items[count].shp_name) - 1] = '\0';
		items[count].asset.mix = "CONQUER.MIX";
		items[count].asset.shp = items[count].shp_name;
		items[count].asset.desc = "hardcoded SHP";
		items[count].asset.frames = k_frames_icon;
		items[count].asset.nframes = (int)(sizeof(k_frames_icon) / sizeof(k_frames_icon[0]));
		count++;
	}

	qsort(items, (size_t)count, sizeof(items[0]), st_menu_cmp_asset_name);
	for (int i = 0; i < count; i++) {
		/* Keep pointer fields coherent after struct moves during qsort. */
		items[i].asset.shp = items[i].shp_name;
	}
	return count;
}

static int st_read_index_line(char *buf, int cap)
{
	int n = 0;
	/* VT52: show text cursor while collecting input. */
	printf("\033e");
	fflush(stdout);
	for (;;) {
		long w = Crawcin();
		unsigned char ch = (unsigned char)(w & 0xFF);
		if (ch == '\r' || ch == '\n') {
			printf("\n");
			fflush(stdout);
			break;
		}
		if (ch == 8 || ch == 127) {
			if (n > 0) {
				n--;
				/* Erase one echoed character. */
				printf("\b \b");
				fflush(stdout);
			}
			continue;
		}
		if (ch >= ' ' && n + 1 < cap) {
			buf[n++] = (char)ch;
			st_console_echo_char((int)ch);
		}
	}
	buf[n] = '\0';
	/* VT52: keep cursor visible after entry as well. */
	printf("\033e");
	fflush(stdout);
	return n;
}

static int st_digits_count(int v)
{
	int d = 1;
	while (v >= 10) {
		v /= 10;
		d++;
	}
	return d;
}

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
 * Keys 1-9 and 0 match array order for the first 10 slots; 's' selects SILOMAKE,
 * 'p' selects POWER, 'r' selects RADAR.GDI, 'i' selects PIPS, '.' selects .SHP,
 * 'b' selects BAR3YLW, and 'd' selects BAR3RED.
 * Other keys default to 0 (E1).
 */
static int st_read_build7_shape_choice(const StBfMenuAsset *items, int count)
{
	if (!items || count <= 0) {
		return 0;
	}

	const int total_digits = st_digits_count(count);
	int max_name_len = 0;
	for (int i = 0; i < count; i++) {
		int n = (int)strlen(items[i].shp_name);
		if (n > max_name_len) {
			max_name_len = n;
		}
	}

	/* Render for 80 columns and fit as many columns as possible. */
	const int gap = 2;
	int cell_w = total_digits + 2 + max_name_len; /* "NNN) NAME" */
	if (cell_w < total_digits + 2 + 8) {
		cell_w = total_digits + 2 + 8;
	}
	int cols = 80 / (cell_w + gap);
	if (cols < 1) {
		cols = 1;
	}

	/* Keep room for header + prompt lines on a 25-line text screen. */
	const int rows = 20;
	const int per_page = rows * cols;
	int page = 0;
	int page_count = (count + per_page - 1) / per_page;

	for (;;) {
		int start = page * per_page;
		int end = start + per_page;
		if (end > count) {
			end = count;
		}
		int page_items = end - start;

		printf("\nTest 7 assets (.SHP), page %d/%d  [n=next p=prev q=quit]\n",
			page + 1, page_count);
		for (int r = 0; r < rows; r++) {
			int printed = 0;
			for (int c = 0; c < cols; c++) {
				int idx = start + c * rows + r;
				if (idx >= end) {
					continue;
				}

				char label[128];
				snprintf(label, sizeof(label), "%*d) %s", total_digits, idx + 1, items[idx].shp_name);
				if ((int)strlen(label) > cell_w) {
					/* Truncate very long names to keep strict column fit. */
					label[cell_w] = '\0';
				}

				if (printed) {
					printf("%*s", gap, "");
				}
				printf("%-*s", cell_w, label);
				printed = 1;
			}
			if (printed) {
				printf("\n");
			}
		}

		printf("Choice [1-%d, n, p, q]: ", count);
		fflush(stdout);
		char line[32];
		st_read_index_line(line, (int)sizeof(line));
		if (!line[0]) {
			continue;
		}

		if (line[1] == '\0') {
			char ch = (char)st_ascii_upper((unsigned char)line[0]);
			if (ch == 'N') {
				if (page + 1 < page_count) {
					page++;
				}
				continue;
			}
			if (ch == 'P') {
				if (page > 0) {
					page--;
				}
				continue;
			}
			if (ch == 'Q') {
				return 0;
			}
		}

		int pick = atoi(line);
		if (pick >= 1 && pick <= count) {
			return pick - 1;
		}
	}
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

static void st_build7_status_line(int remap_index, int page_index, int page_count,
    unsigned short tw, unsigned short th, unsigned short tc)
{
	/*
	 * VT52 direct cursor address:
	 *   ESC Y row+32 col+32
	 * ST low-res text console is 40x25, so row 24 is the bottom line.
	 */
	printf("\033Y%c%c", (char)(24 + 32), (char)(0 + 32));
	printf(
	    "%ux%u F%u %s P:%d/%d SPC H Y",
	    (unsigned)tw,
	    (unsigned)th,
	    (unsigned)tc,
	    st_build7_remap_name(remap_index),
	    page_index + 1,
	    page_count);
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

static int st_bf_autocheck_eligible(const StBfAsset *a)
{
	if (!a || !a->shp) {
		return 0;
	}
	if (!st_has_shp_ext(a->shp)) {
		return 0;
	}
	/* Edge-case menu entry; not a real MIX filename. */
	if (strcmp(a->shp, ".SHP") == 0) {
		return 0;
	}
	return 1;
}

static int st_bf_conquer_mix_reachable(void)
{
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int const mx = st_extract_asset_preferring_dos_conquer("CONQUER.MIX", "E1.SHP", &raw, &raw_len);
	int const ok = (mx == 0 && raw != NULL && raw_len > 0);
	if (raw) {
		free(raw);
	}
	return ok;
}

/*
 * Returns number of failed Build_Frame calls (0 = all decodes returned non-NULL).
 */
int st_run_build_frame_asset_autocheck_ex(
	int verbose,
	int *out_ok,
	int *out_skip,
	int *out_fail,
	StAutotestStatus *out_status)
{
	int fails = 0;
	int ok = 0;
	int skip = 0;

	if (out_ok) {
		*out_ok = 0;
	}
	if (out_skip) {
		*out_skip = 0;
	}
	if (out_fail) {
		*out_fail = 0;
	}
	if (out_status) {
		*out_status = ST_AUTO_PASS;
	}

	if (!st_bf_conquer_mix_reachable()) {
		if (verbose) {
			printf("SKIP Build_Frame (CONQUER.MIX / E1.SHP not available)\n");
		}
		if (out_status) {
			*out_status = ST_AUTO_SKIP;
		}
		return 0;
	}

	for (size_t ai = 0; ai < sizeof(k_assets) / sizeof(k_assets[0]); ai++) {
		const StBfAsset *a = &k_assets[ai];
		if (!st_bf_autocheck_eligible(a)) {
			continue;
		}
		unsigned char *raw = NULL;
		size_t raw_len = 0;
		int mx = st_extract_asset_preferring_dos_conquer(a->mix, a->shp, &raw, &raw_len);
		if (mx != 0 || !raw) {
			skip++;
			if (verbose) {
				printf("SKIP %s:%s err=%d\n", a->mix, a->shp, mx);
			}
			continue;
		}
		ok++;
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

	if (out_ok) {
		*out_ok = ok;
	}
	if (out_skip) {
		*out_skip = skip;
	}
	if (out_fail) {
		*out_fail = fails;
	}
	if (out_status) {
		if (fails > 0) {
			*out_status = ST_AUTO_FAIL;
		} else if (ok == 0) {
			*out_status = ST_AUTO_FAIL;
		} else {
			*out_status = ST_AUTO_PASS;
		}
	}
	return fails;
}

int st_run_build_frame_asset_autocheck(void)
{
	StAutotestStatus st = ST_AUTO_PASS;
	return st_run_build_frame_asset_autocheck_ex(1, NULL, NULL, NULL, &st);
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
		SuperToUser(old_ssp);
		printf("FAIL: oom chunky\n");
		return 1;
	}

	StBfMenuAsset menu_assets[1024];
	int menu_count = st_build7_collect_assets(menu_assets, (int)(sizeof(menu_assets) / sizeof(menu_assets[0])));
	const int pick = st_read_build7_shape_choice(menu_assets, menu_count);
	StBfMenuAsset *picked = &menu_assets[(pick >= 0 && pick < menu_count) ? pick : 0];
	StBfAsset sel_copy = picked->asset;
	sel_copy.shp = picked->shp_name;
	const StBfAsset *sel = &sel_copy;

	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int mx = st_extract_asset_preferring_dos_conquer(sel->mix, sel->shp, &raw, &raw_len);
	if (mx != 0 || !raw) {
		printf("SKIP %s:%s err=%d (%s)\n", sel->mix, sel->shp, mx, sel->desc);
		free(chunky);
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
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
		SuperToUser(old_ssp);
		return 1;
	}

	unsigned long const buf_need = Get_Build_Frame_BufferBytes(raw);
	printf(
	    "%s:%s keyframe size %ux%u px (all frames), %u frames, buffer %lu bytes\n",
	    sel->mix,
	    sel->shp,
	    (unsigned)tw,
	    (unsigned)th,
	    (unsigned)tc,
	    (unsigned long)buf_need);

	C2P_Load_WeightSet("TEMPERAT", "Build frame assets");
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
		SuperToUser(old_ssp);
		return 1;
	}
	C2P_Render_Logical_To_ST_Screen(chunky, ST_SCR_W, planar, 0, C2P_ST_SCREEN_HEIGHT, 1);
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
		C2P_Render_Logical_To_ST_Screen(chunky, ST_SCR_W, planar, 0, C2P_ST_SCREEN_HEIGHT, 1);
		Setscreen((long)planar, (long)planar, -1L);
		Vsync();
		Vsync();
		st_build7_status_line(remap_index, page_index, page_count, tw, th, tc);

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
	SuperToUser(old_ssp);
	return ok ? 0 : 1;
}
