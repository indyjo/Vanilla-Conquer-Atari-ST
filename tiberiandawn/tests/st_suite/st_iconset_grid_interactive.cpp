/*
 * Interactive: pick an ICN/.TEM/.WIN/.DES iconset from TEMPERAT/WINTER/DESERT.MIX (or loose
 * file) and tile every logical cell (IControl Count, via Map) on a 320x200 planar screen.
 * Unmasked iconsets use ST16; masked (TransFlag) stay on live 8bpp->C2P.
 * Logical map slots mapped to 0xFF (unused template cells) are skipped here;
 * the grid still outlines them — same as TemplateClass::Mark in-game.
 *
 * Needs at least one of TEMPERAT/WINTER/DESERT.MIX + matching .W16 next to tstcnc.tos.
 * p / n or space: prev / next iconset in the available list.
 * m: return to numbered menu (digits + Enter, n/p page).
 * q / ESC: quit.
 */

#include "st_iconset_grid_interactive.h"

#include "c2p.h"
#include "function.h"
#include "gbuffer.h"
#include "palette.h"
#include "st16_convert.h"
#include "st16_draw.h"
#include "st16_iconset.h"
#include "tile.h"
#include "st_mix_register.h"
#include "st_temperat_palette.h"
#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <string.h>

enum {
	ST_SCR_W = 320,
	ST_SCR_H = 200,
	ST_HW_PAL_COUNT = 16,
	ST_ICONSET_STATUS_ROW = 20
};

/*
 * Temperate theater template / icon names (IniName + .TEM) plus TRANS.ICN.
 * Only entries that MFCD::Retrieve can resolve are shown.
 */
static const char *const k_iconset_candidates[] = {
	/* TRANS.ICN: theater-independent */
	"TRANS.ICN",

	/* ------ TEMPERATE (*.TEM) ------ */
	"CLEAR1.TEM",
	/* Water / shores */
	"W1.TEM", "W2.TEM",
	"SH1.TEM",  "SH2.TEM",  "SH3.TEM",  "SH4.TEM",  "SH5.TEM",
	"SH6.TEM",  "SH7.TEM",  "SH8.TEM",  "SH9.TEM",  "SH10.TEM",
	"SH11.TEM", "SH12.TEM", "SH13.TEM", "SH14.TEM", "SH15.TEM",
	"SH16.TEM", "SH17.TEM", "SH18.TEM",
	/* Roads */
	"D01.TEM", "D02.TEM", "D03.TEM", "D04.TEM", "D05.TEM",
	"D06.TEM", "D07.TEM", "D08.TEM", "D09.TEM", "D10.TEM",
	"D11.TEM", "D12.TEM", "D13.TEM", "D14.TEM", "D15.TEM",
	"D16.TEM", "D17.TEM", "D18.TEM", "D19.TEM", "D20.TEM",
	"D21.TEM", "D22.TEM", "D23.TEM", "D24.TEM", "D25.TEM",
	"D26.TEM", "D27.TEM", "D28.TEM", "D29.TEM", "D30.TEM",
	"D31.TEM", "D32.TEM", "D33.TEM", "D34.TEM", "D35.TEM",
	"D36.TEM", "D37.TEM", "D38.TEM", "D39.TEM", "D40.TEM",
	"D41.TEM", "D42.TEM", "D43.TEM",
	/* Boulders */
	"B1.TEM", "B2.TEM", "B3.TEM",
	/* Slopes */
	"S01.TEM", "S02.TEM", "S03.TEM", "S04.TEM", "S05.TEM",
	"S06.TEM", "S07.TEM", "S08.TEM", "S09.TEM", "S10.TEM",
	"S11.TEM", "S12.TEM", "S13.TEM", "S14.TEM", "S15.TEM",
	"S16.TEM", "S17.TEM", "S18.TEM", "S19.TEM", "S20.TEM",
	"S21.TEM", "S22.TEM", "S23.TEM", "S24.TEM", "S25.TEM",
	"S26.TEM", "S27.TEM", "S28.TEM", "S29.TEM", "S30.TEM",
	"S31.TEM", "S32.TEM", "S33.TEM", "S34.TEM", "S35.TEM",
	"S36.TEM", "S37.TEM", "S38.TEM",
	/* Trees */
	"T01.TEM", "T02.TEM", "T03.TEM", "T05.TEM", "T06.TEM",
	"T07.TEM", "T08.TEM", "T10.TEM", "T11.TEM", "T12.TEM",
	"T13.TEM", "T14.TEM", "T15.TEM", "T16.TEM",
	/* Patches */
	"P01.TEM", "P02.TEM", "P03.TEM", "P04.TEM",
	"P07.TEM", "P08.TEM", "P13.TEM", "P14.TEM", "P15.TEM",
	/* Rivers, falls, bridges */
	"RV01.TEM", "RV02.TEM", "RV03.TEM", "RV04.TEM", "RV05.TEM",
	"RV06.TEM", "RV07.TEM", "RV08.TEM", "RV09.TEM", "RV10.TEM",
	"RV11.TEM", "RV12.TEM", "RV13.TEM",
	"FALLS1.TEM", "FALLS2.TEM",
	"BRIDGE1.TEM", "BRIDGE1D.TEM", "BRIDGE2.TEM", "BRIDGE2D.TEM",

	/* ------ WINTER (*.WIN) ------ */
	"CLEAR1.WIN",
	"W1.WIN",   "W2.WIN",
	"SH1.WIN",  "SH2.WIN",  "SH3.WIN",  "SH4.WIN",  "SH5.WIN",
	"SH6.WIN",  "SH7.WIN",  "SH8.WIN",  "SH9.WIN",  "SH10.WIN",
	"SH11.WIN", "SH12.WIN", "SH13.WIN", "SH14.WIN", "SH15.WIN",
	"SH16.WIN", "SH17.WIN", "SH18.WIN",
	"D01.WIN", "D02.WIN", "D03.WIN", "D04.WIN", "D05.WIN",
	"D06.WIN", "D07.WIN", "D08.WIN", "D09.WIN", "D10.WIN",
	"D11.WIN", "D12.WIN", "D13.WIN", "D14.WIN", "D15.WIN",
	"D16.WIN", "D17.WIN", "D18.WIN", "D19.WIN", "D20.WIN",
	"D21.WIN", "D22.WIN", "D23.WIN", "D24.WIN", "D25.WIN",
	"D26.WIN", "D27.WIN", "D28.WIN", "D29.WIN", "D30.WIN",
	"D31.WIN", "D32.WIN", "D33.WIN", "D34.WIN", "D35.WIN",
	"D36.WIN", "D37.WIN", "D38.WIN", "D39.WIN", "D40.WIN",
	"D41.WIN", "D42.WIN", "D43.WIN",
	"B1.WIN", "B2.WIN", "B3.WIN",
	"S01.WIN", "S02.WIN", "S03.WIN", "S04.WIN", "S05.WIN",
	"S06.WIN", "S07.WIN", "S08.WIN", "S09.WIN", "S10.WIN",
	"S11.WIN", "S12.WIN", "S13.WIN", "S14.WIN", "S15.WIN",
	"S16.WIN", "S17.WIN", "S18.WIN", "S19.WIN", "S20.WIN",
	"S21.WIN", "S22.WIN", "S23.WIN", "S24.WIN", "S25.WIN",
	"S26.WIN", "S27.WIN", "S28.WIN", "S29.WIN", "S30.WIN",
	"S31.WIN", "S32.WIN", "S33.WIN", "S34.WIN", "S35.WIN",
	"S36.WIN", "S37.WIN", "S38.WIN",
	"T01.WIN", "T02.WIN", "T03.WIN", "T05.WIN", "T06.WIN",
	"T07.WIN", "T08.WIN", "T10.WIN", "T11.WIN", "T12.WIN",
	"T13.WIN", "T14.WIN", "T15.WIN", "T16.WIN",
	"P07.WIN", "P08.WIN", "P13.WIN", "P14.WIN", "P15.WIN",
	"P16.WIN", "P17.WIN", "P18.WIN", "P19.WIN", "P20.WIN",
	"RV01.WIN", "RV02.WIN", "RV03.WIN", "RV04.WIN", "RV05.WIN",
	"RV06.WIN", "RV07.WIN", "RV08.WIN", "RV09.WIN", "RV10.WIN",
	"RV11.WIN", "RV12.WIN", "RV13.WIN",
	"FALLS1.WIN", "FALLS2.WIN",
	"BRIDGE1.WIN", "BRIDGE1D.WIN", "BRIDGE2.WIN", "BRIDGE2D.WIN",

	/* ------ DESERT (*.DES) ------ */
	"CLEAR1.DES",
	"W1.DES",
	"SH17.DES", "SH18.DES",
	"SH19.DES", "SH20.DES", "SH21.DES", "SH22.DES", "SH23.DES",
	"SH25.DES", "SH26.DES", "SH27.DES", "SH28.DES",
	"SH30.DES", "SH31.DES",
	"SH36.DES", "SH37.DES", "SH38.DES", "SH39.DES",
	"SH40.DES", "SH42.DES", "SH43.DES", "SH44.DES", "SH45.DES",
	"SH47.DES", "SH48.DES", "SH49.DES",
	"SH51.DES", "SH52.DES",
	"SH54.DES", "SH55.DES", "SH56.DES", "SH57.DES",
	"SH58.DES", "SH59.DES", "SH61.DES", "SH62.DES", "SH63.DES",
	"D01.DES", "D02.DES", "D03.DES", "D04.DES", "D05.DES",
	"D06.DES", "D07.DES", "D08.DES", "D09.DES", "D10.DES",
	"D11.DES", "D12.DES", "D13.DES", "D14.DES", "D15.DES",
	"D16.DES", "D17.DES", "D18.DES", "D19.DES", "D20.DES",
	"D21.DES", "D22.DES", "D23.DES", "D24.DES", "D25.DES",
	"D26.DES", "D27.DES", "D28.DES", "D29.DES", "D30.DES",
	"D31.DES", "D32.DES", "D33.DES", "D34.DES", "D35.DES",
	"D36.DES", "D37.DES", "D38.DES", "D39.DES", "D40.DES",
	"D41.DES", "D42.DES", "D43.DES",
	"B1.DES", "B2.DES", "B4.DES", "B5.DES", "B6.DES",
	"S01.DES", "S02.DES", "S03.DES", "S04.DES", "S05.DES",
	"S06.DES", "S07.DES", "S08.DES", "S09.DES", "S10.DES",
	"S11.DES", "S12.DES", "S13.DES", "S14.DES", "S15.DES",
	"S16.DES", "S17.DES", "S18.DES", "S19.DES", "S20.DES",
	"S21.DES", "S22.DES", "S23.DES", "S24.DES", "S25.DES",
	"S26.DES", "S27.DES", "S28.DES", "S29.DES", "S30.DES",
	"S31.DES", "S32.DES", "S33.DES", "S34.DES", "S35.DES",
	"S36.DES", "S37.DES", "S38.DES",
	"T04.DES", "T07.DES", "T08.DES", "T09.DES", "T17.DES", "T18.DES",
	"P01.DES", "P02.DES", "P03.DES", "P04.DES",
	"P05.DES", "P06.DES", "P07.DES", "P08.DES",
	"BR1.DES",  "BR2.DES",  "BR3.DES",  "BR4.DES",  "BR5.DES",
	"BR6.DES",  "BR7.DES",  "BR8.DES",  "BR9.DES",  "BR10.DES",
	"RV14.DES", "RV15.DES", "RV16.DES", "RV17.DES", "RV18.DES",
	"RV19.DES", "RV20.DES", "RV21.DES", "RV22.DES", "RV23.DES",
	"RV24.DES", "RV25.DES",
	"FALLS1.DES", "FALLS2.DES",
	"BRIDGE3.DES", "BRIDGE3D.DES", "BRIDGE4.DES", "BRIDGE4D.DES",
};

static const int k_iconset_candidate_count =
	(int)(sizeof(k_iconset_candidates) / sizeof(k_iconset_candidates[0]));

/* Return the C2P weight-set name for a given iconset filename based on extension. */
static const char *st_theater_from_name(const char *name)
{
	const char *dot = name ? strrchr(name, '.') : NULL;

	if (dot) {
		if (dot[1] == 'W' || dot[1] == 'w') {
			return "WINTER";
		}
		if (dot[1] == 'D' || dot[1] == 'd') {
			return "DESERT";
		}
	}
	return "TEMPERAT";
}

static uint8_t g_iconset_chunky_scratch[ST16_CHUNKY_ICON_MAX_BYTES];

static void st_hw_palette_read(unsigned short *dst16)
{
	volatile unsigned short *pr = (volatile unsigned short *)0xFF8240L;
	int i;

	for (i = 0; i < ST_HW_PAL_COUNT; ++i) {
		dst16[i] = pr[i];
	}
}

static void st_hw_palette_write(const unsigned short *src16)
{
	volatile unsigned short *pr = (volatile unsigned short *const)0xFF8240L;
	int i;

	for (i = 0; i < ST_HW_PAL_COUNT; ++i) {
		pr[i] = src16[i];
	}
}

static int st_iconset_is_available(const char *name)
{
	return MFCD::Retrieve(name) != NULL;
}

static void st_console_goto_row(int row_1based)
{
	printf("\033Y%c%c", (char)(row_1based + 32), (char)(0 + 32));
	fflush(stdout);
}

static int st_iconset_pick_index(int avail_idx[], int avail_count)
{
	enum { PAGE_SIZE = 20 };
	int page = 0;
	int page_count = (avail_count + PAGE_SIZE - 1) / PAGE_SIZE;

	for (;;) {
		int page_start = page * PAGE_SIZE;
		int page_end = page_start + PAGE_SIZE;
		int page_items;
		char buf[12];
		int len = 0;

		if (page_end > avail_count) {
			page_end = avail_count;
		}
		page_items = page_end - page_start;

		printf("\n---- Iconset grid (%d avail, pg %d/%d) ----\n",
			avail_count, page + 1, page_count);
		for (int j = page_start; j < page_end; ++j) {
			const int ci = avail_idx[j];
			void const *ptr = MFCD::Retrieve(k_iconset_candidates[ci]);
			ST16_IControlView ic;
			uint32_t total = 0;
			int ok = 0;

			if (ptr) {
				const uint8_t *raw = (const uint8_t *)ptr;
				const IControl_Type *nic = (const IControl_Type *)ptr;

				if (ST16_Has_Native_Chunk(nic)) {
					ic.width = (uint16_t)nic->Width;
					ic.height = (uint16_t)nic->Height;
					ic.count = (uint16_t)nic->Count;
					ic.size = (uint32_t)nic->Size;
					ic.icons_off = (uint32_t)nic->Icons;
					ic.map_off = (uint32_t)nic->Map;
					ic.transflag_off = (uint32_t)nic->TransFlag;
					ok = (ic.width > 0 && ic.height > 0 && ic.count > 0);
				} else {
					total = ST16_Read_LE32(raw + 8);
					if (total >= ST16_ICONTROL_SIZE) {
						ok = ST16_Parse_IControl(raw, (size_t)total, &ic);
					}
				}
			}
			if (ok) {
				printf(
					"%2d %-12s %ux%u x%u\n",
					j - page_start + 1,
					k_iconset_candidates[ci],
					(unsigned)ic.width,
					(unsigned)ic.height,
					(unsigned)ic.count);
			} else {
				printf("%2d %-12s (bad header)\n", j - page_start + 1, k_iconset_candidates[ci]);
			}
		}
		if (page_count > 1) {
			printf("p prev page  n next page  0 Back\n");
		} else {
			printf("0 Back\n");
		}
		printf("Choice (1-%d + Enter, or n/p page): ", page_items);
		fflush(stdout);

		for (;;) {
			long w = Crawcin();
			int ch = (int)(w & 0xFF);

			if (ch == 27) {
				printf("\n");
				return -1;
			}
			if (ch == '0' && len == 0) {
				printf("0\n");
				return -1;
			}
			/* Single-key page nav (same as other ST test menus; avoids -/+ intl keyboard issues). */
			if (len == 0 && (ch == 'n' || ch == 'N')) {
				printf("n\n");
				if (page + 1 < page_count) {
					++page;
				}
				break;
			}
			if (len == 0 && (ch == 'p' || ch == 'P')) {
				printf("p\n");
				if (page > 0) {
					--page;
				}
				break;
			}
			if (ch == '\r' || ch == '\n') {
				printf("\n");
				if (len <= 0) {
					continue;
				}
				buf[len] = '\0';
				{
					int val = 0;
					int i;
					int bad = 0;

					for (i = 0; buf[i] != '\0'; ++i) {
						if (buf[i] < '0' || buf[i] > '9') {
							bad = 1;
							break;
						}
						val = val * 10 + (buf[i] - '0');
					}
					if (bad) {
						printf("Unknown option.\n");
					} else if (val >= 1 && val <= page_items) {
						return avail_idx[page_start + val - 1];
					} else {
						printf("Unknown option %d.\n", val);
					}
					len = 0;
					printf("Choice (1-%d + Enter, or n/p page): ", page_items);
					fflush(stdout);
				}
				continue;
			}
			if (ch == '\b' || ch == 127) {
				if (len > 0) {
					--len;
					printf("\b \b");
					fflush(stdout);
				}
				continue;
			}
			if (ch >= '0' && ch <= '9' && len + 1 < (int)sizeof(buf)) {
				buf[len++] = (char)ch;
				printf("%c", ch);
				fflush(stdout);
			}
		}
	}
}

static int st_iconset_vp_planar_target(
	GraphicViewPortClass &vp,
	uint8_t **root_out,
	int *row_bytes_out,
	int *width_out,
	int *height_out,
	int *xpos_out,
	int *ypos_out)
{
	GraphicBufferClass *gb = vp.Get_Graphic_Buffer();

	if (!gb || !gb->Is_ST_Planar() || !root_out || !row_bytes_out || !width_out || !height_out) {
		return 0;
	}

	*root_out = (uint8_t *)gb->Get_Buffer();
	*row_bytes_out = gb->Get_Pitch();
	if (*row_bytes_out <= 0) {
		*row_bytes_out = ST_PLANAR_BYTES_PER_LINE;
	}
	*width_out = gb->Get_Width();
	*height_out = gb->Get_Height();
	if (xpos_out) {
		*xpos_out = vp.Get_XPos();
	}
	if (ypos_out) {
		*ypos_out = vp.Get_YPos();
	}
	return (*root_out && *width_out > 0 && *height_out > 0) ? 1 : 0;
}

/* Outline every logical map slot so empty/transparent cells are visible on black. */
/* TRUE when Map[logical] resolves to a real image (not 0xFF / out of range). */
static int st_iconset_logical_slot_has_image(const uint8_t *raw, size_t blob_size, int logical)
{
	int image_index;

	return ST16_Resolve_Icon_Index(raw, blob_size, logical, &image_index) ? 1 : 0;
}

static void st_iconset_draw_grid_frames(
	GraphicViewPortClass &vp,
	int tile_w,
	int tile_h,
	int count,
	int cols,
	unsigned char color)
{
	int i;

	if (tile_w <= 0 || tile_h <= 0 || count <= 0 || cols <= 0) {
		return;
	}

	for (i = 0; i < count; ++i) {
		const int col = i % cols;
		const int row = i / cols;
		const int x = col * tile_w;
		const int y = row * tile_h;

		if (y + tile_h > vp.Get_Height()) {
			break;
		}
		vp.Draw_Rect(x, y, x + tile_w - 1, y + tile_h - 1, color);
	}
}

static int st_iconset_paint_grid(GraphicViewPortClass &vp, void const *icondata, char *mode_out, size_t mode_cap)
{
	const uint8_t *raw = (const uint8_t *)icondata;
	ST16_IControlView ic;
	uint32_t total;
	uint8_t *screen = NULL;
	int row_bytes = 0;
	int screen_w = 0;
	int screen_h = 0;
	int vp_x = 0;
	int vp_y = 0;
	int cols;
	int i;
	int used_st16 = 0;

	if (mode_out && mode_cap > 0) {
		mode_out[0] = '\0';
	}

	if (!raw) {
		return 0;
	}
	/* Already-native (previously converted in-place): read fields directly. */
	if (ST16_Has_Native_Chunk((const IControl_Type *)raw)) {
		const IControl_Type *nic = (const IControl_Type *)raw;
		ic.width = (uint16_t)nic->Width;
		ic.height = (uint16_t)nic->Height;
		ic.count = (uint16_t)nic->Count;
		ic.size = (uint32_t)nic->Size;
		ic.icons_off = (uint32_t)nic->Icons;
		ic.map_off = (uint32_t)nic->Map;
		ic.transflag_off = (uint32_t)nic->TransFlag;
		total = ic.size;
		if (ic.width <= 0 || ic.height <= 0 || ic.count <= 0) {
			return 0;
		}
	} else {
		total = ST16_Read_LE32(raw + 8);
		if (total < ST16_ICONTROL_SIZE || !ST16_Parse_IControl(raw, (size_t)total, &ic)) {
			return 0;
		}
	}
	if (ic.width <= 0 || ic.height <= 0 || ic.count <= 0 || ic.width > 128 || ic.height > 128) {
		return 0;
	}
	if (!C2P_Weights_Are_Ready()) {
		if (mode_out && mode_cap > 0) {
			strncpy(mode_out, "no C2P weights", mode_cap - 1);
			mode_out[mode_cap - 1] = '\0';
		}
		return 0;
	}
	if (!st_iconset_vp_planar_target(
			vp, &screen, &row_bytes, &screen_w, &screen_h, &vp_x, &vp_y)) {
		return 0;
	}

	vp.Clear(0);
	cols = vp.Get_Width() / (int)ic.width;
	if (cols <= 0) {
		cols = 1;
	}

	{
		const void *resolved = ST16_Iconset_Resolve(icondata, g_iconset_chunky_scratch);

		if (resolved) {
			raw = (const uint8_t *)resolved;
			if (ST16_Has_Native_Chunk((const IControl_Type *)raw)) {
				const IControl_Type *nic = (const IControl_Type *)raw;
				total = (uint32_t)nic->Size;
				ic.width = (uint16_t)nic->Width;
				ic.height = (uint16_t)nic->Height;
				ic.count = (uint16_t)nic->Count;
				ic.size = total;
				ic.icons_off = (uint32_t)nic->Icons;
				ic.map_off = (uint32_t)nic->Map;
				ic.transflag_off = (uint32_t)nic->TransFlag;
			} else {
				total = ST16_Read_LE32(raw + 8);
				if (!ST16_Parse_IControl(raw, (size_t)total, &ic)) {
					total = 0;
				}
			}
		}
	}
	if (ST16_Has_Native_Chunk((const IControl_Type *)raw)) {
		for (i = 0; i < (int)ic.count; ++i) {
			const int col = i % cols;
			const int row = i / cols;
			const int x = col * (int)ic.width;
			const int y = row * (int)ic.height;

			if (y + (int)ic.height > vp.Get_Height()) {
				break;
			}
			if (!st_iconset_logical_slot_has_image(raw, (size_t)total, i)) {
				continue;
			}
			ST16_Blit_Stamp(&vp, (const IControl_Type *)raw, i, x, y);
		}
		used_st16 = 1;
	}

	if (!used_st16) {
		for (i = 0; i < (int)ic.count; ++i) {
			int image_index = i;
			const uint8_t *icon;
			const int col = i % cols;
			const int row = i / cols;
			const int x = col * (int)ic.width;
			const int y = row * (int)ic.height;

			if (y + (int)ic.height > vp.Get_Height()) {
				break;
			}
			if (!ST16_Resolve_Icon_Index(raw, (size_t)total, i, &image_index)) {
				continue;
			}
			icon = ST16_Standard_Icon_Ptr(raw, (size_t)total, image_index);
			if (!icon) {
				continue;
			}
			C2P_Render_Logical_To_Planar_Rect(
				icon,
				(int)ic.width,
				(int)ic.height,
				(int)ic.width,
				screen,
				row_bytes,
				screen_w,
				screen_h,
				vp_x + x,
				vp_y + y,
				0,
				0);
		}
	}

	/* Show cell boundaries even when a slot is empty, transparent, or black. */
	st_iconset_draw_grid_frames(vp, (int)ic.width, (int)ic.height, (int)ic.count, cols, 15);

	if (mode_out && mode_cap > 0) {
		if (used_st16) {
			strncpy(mode_out, "ST16", mode_cap - 1);
		} else if (ST16_Iconset_Uses_Mask(raw, (size_t)total)) {
			strncpy(mode_out, "masked C2P", mode_cap - 1);
		} else {
			strncpy(mode_out, "C2P 8bpp", mode_cap - 1);
		}
		mode_out[mode_cap - 1] = '\0';
	}
	return 1;
}

static int st_iconset_grid_catalog_index(int avail_idx[], int max_avail)
{
	int count = 0;
	int i;

	for (i = 0; i < k_iconset_candidate_count && count < max_avail; ++i) {
		if (st_iconset_is_available(k_iconset_candidates[i])) {
			avail_idx[count++] = i;
		}
	}
	return count;
}

int st_run_interactive_iconset_grid(void)
{
	unsigned short saved_hw[ST_HW_PAL_COUNT];
	int avail_idx[300];
	int avail_count;
	int catalog_index = 0;
	int old_rez;
	long old_phys;
	long old_log;
	long old_ssp;

	old_ssp = Super(0L);
	old_rez = Getrez();
	old_phys = (long)Physbase();
	old_log = (long)Logbase();
	st_hw_palette_read(saved_hw);
	Setscreen(-1L, -1L, 0);

	(void)st_tests_register_mixes_once();
	avail_count = st_iconset_grid_catalog_index(avail_idx, (int)(sizeof(avail_idx) / sizeof(avail_idx[0])));
	if (avail_count <= 0) {
		st_hw_palette_write(saved_hw);
		Setscreen(old_log, old_phys, old_rez);
		SuperToUser(old_ssp);
		printf("Iconset grid: no catalog entries in MIX/cwd.\n");
		printf("(Need TEMPERAT/WINTER/DESERT.MIX beside tstcnc.tos.)\n");
		return 1;
	}

	{
		unsigned char *tos_screen = (unsigned char *)Logbase();
		GraphicBufferClass screen;
		GraphicViewPortClass vp;

		if (!tos_screen) {
			st_hw_palette_write(saved_hw);
			Setscreen(old_log, old_phys, old_rez);
			SuperToUser(old_ssp);
			printf("SKIP: no logbase\n");
			return 1;
		}

		screen.Init(ST_SCR_W, ST_SCR_H, tos_screen, 32768L, (int)GBC_ST_PLANAR_LORES);
		vp.Attach(&screen, 0, 0, ST_SCR_W, ST_SCR_H);
		Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);

		C2P_Load_WeightSet("TEMPERAT", "Iconset grid");
		{
			unsigned char pal[768];
			memcpy(pal, kStTemperatPal768, sizeof(pal));
			Set_Palette(pal);
		}

		printf("\nIconset grid (production Draw_Stamp / ST16).\n");
		printf("p prev  n/space next  m menu  q/ESC quit\n");

		catalog_index = st_iconset_pick_index(avail_idx, avail_count);
		if (catalog_index < 0) {
			st_hw_palette_write(saved_hw);
			Setscreen(old_log, old_phys, old_rez);
			SuperToUser(old_ssp);
			return 0;
		}

		{
		const char *cur_theater = NULL;
		for (;;) {
			const char *theater = st_theater_from_name(k_iconset_candidates[catalog_index]);

			if (theater != cur_theater) {
				if (!C2P_Load_WeightSet(theater, "Iconset grid")) {
					/* Fall back to TEMPERAT weights if the theater's .W16 is absent */
					theater = "TEMPERAT";
					C2P_Load_WeightSet(theater, "Iconset grid");
				}
				cur_theater = theater;
				/* Load the theater palette from MIX when possible; keep temperat for .ICN */
				if (strcmp(theater, "TEMPERAT") == 0) {
					unsigned char pal[768];
					memcpy(pal, kStTemperatPal768, sizeof(pal));
					Set_Palette(pal);
				} else {
					char palname[16];
					const void *palptr;
					snprintf(palname, sizeof(palname), "%s.PAL", theater);
					palptr = MFCD::Retrieve(palname);
					if (palptr) {
						unsigned char pal[768];
						memcpy(pal, palptr, 768);
						Set_Palette(pal);
					}
				}
			}
			void const *icondata = MFCD::Retrieve(k_iconset_candidates[catalog_index]);
			ST16_IControlView ic;
			uint32_t total = 0;
			int parsed = 0;

			if (icondata) {
				const uint8_t *raw = (const uint8_t *)icondata;
				const IControl_Type *nic = (const IControl_Type *)icondata;
				if (ST16_Has_Native_Chunk(nic)) {
					ic.width = (uint16_t)nic->Width;
					ic.height = (uint16_t)nic->Height;
					ic.count = (uint16_t)nic->Count;
					ic.size = (uint32_t)nic->Size;
					ic.icons_off = (uint32_t)nic->Icons;
					ic.map_off = (uint32_t)nic->Map;
					ic.transflag_off = (uint32_t)nic->TransFlag;
					parsed = (ic.width > 0 && ic.height > 0 && ic.count > 0);
				} else {
					total = ST16_Read_LE32(raw + 8);
					if (total >= ST16_ICONTROL_SIZE) {
						parsed = ST16_Parse_IControl(raw, (size_t)total, &ic);
					}
				}
			}

			char draw_mode[16];

			draw_mode[0] = '\0';
			st_iconset_paint_grid(vp, icondata, draw_mode, sizeof(draw_mode));
			Setscreen((long)screen.Get_Buffer(), (long)screen.Get_Buffer(), -1L);
			st_console_goto_row(ST_ICONSET_STATUS_ROW);

			if (parsed) {
				st_wrap_puts(k_iconset_candidates[catalog_index], ST_TEXT_MAXCOL);
				{
					char line[48];
					if (draw_mode[0] != '\0') {
						sprintf(
							line,
							"%ux%u x%u  %s",
							(unsigned)ic.width,
							(unsigned)ic.height,
							(unsigned)ic.count,
							draw_mode);
					} else {
						sprintf(
							line,
							"%ux%u x%u",
							(unsigned)ic.width,
							(unsigned)ic.height,
							(unsigned)ic.count);
					}
					st_wrap_puts(line, ST_TEXT_MAXCOL);
				}
			} else {
				st_wrap_puts("bad iconset header", ST_TEXT_MAXCOL);
			}

			long w = Crawcin();
			int ch = (int)(w & 0xFF);
			if (ch == 'q' || ch == 'Q' || ch == 27) {
				break;
			}
			if (ch == 'p' || ch == 'P') {
				int slot = 0;
				for (int j = 0; j < avail_count; ++j) {
					if (avail_idx[j] == catalog_index) {
						slot = j;
						break;
					}
				}
				slot = (slot + avail_count - 1) % avail_count;
				catalog_index = avail_idx[slot];
				continue;
			}
			if (ch == 'm' || ch == 'M') {
				int picked = st_iconset_pick_index(avail_idx, avail_count);
				if (picked < 0) {
					break;
				}
				catalog_index = picked;
				continue;
			}
			if (ch == 'n' || ch == 'N' || ch == ' ') {
				int slot = 0;
				for (int j = 0; j < avail_count; ++j) {
					if (avail_idx[j] == catalog_index) {
						slot = j;
						break;
					}
				}
				slot = (slot + 1) % avail_count;
				catalog_index = avail_idx[slot];
				continue;
			}
		}
		}
	}

	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return 0;
}
