/*
 * Interactive: pick an ICN/.TEM iconset from TEMPERAT.MIX (or loose file) and
 * Tiles every logical cell (IControl Count, via Map) on a 320x200 planar screen.
 * Unmasked iconsets use ST16; masked (TransFlag) stay on live 8bpp->C2P.
 *
 * Needs TEMPERAT.MIX + TEMPERAT.W16 next to tstcnc.tos.
 * p / n or space: prev / next iconset in the available list.
 * m: return to numbered menu (digits + Enter).
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
	"TRANS.ICN",
	"CLEAR1.TEM",
	"W1.TEM",
	"W2.TEM",
	"D01.TEM",
	"D05.TEM",
	"D10.TEM",
	"D20.TEM",
	"D30.TEM",
	"D40.TEM",
	"SH1.TEM",
	"SH3.TEM",
	"SH5.TEM",
	"SH10.TEM",
	"SH15.TEM",
	"SH20.TEM",
	"SH25.TEM",
	"SH30.TEM",
	"S01.TEM",
	"S02.TEM",
	"S05.TEM",
	"B1.TEM",
	"B6.TEM",
	"T01.TEM",
	"T02.TEM",
	"T05.TEM",
	"P01.TEM",
	"P02.TEM",
};

static const int k_iconset_candidate_count =
	(int)(sizeof(k_iconset_candidates) / sizeof(k_iconset_candidates[0]));

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

static int st_read_decimal_choice(void)
{
	char buf[12];
	int len = 0;

	printf("(digits + Enter, Esc cancel)\n");
	fflush(stdout);

	for (;;) {
		long w = Crawcin();
		int ch = (int)(w & 0xFF);

		if (ch == 27) {
			printf("\n");
			return -1;
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

				for (i = 0; buf[i] != '\0'; ++i) {
					if (buf[i] < '0' || buf[i] > '9') {
						return -2;
					}
					val = val * 10 + (buf[i] - '0');
				}
				return val;
			}
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

static int st_iconset_pick_index(int avail_idx[], int avail_count)
{
	for (;;) {
		int pick;

		printf("\n---- Iconset grid (%d available) ----\n", avail_count);
		for (int j = 0; j < avail_count; ++j) {
			const int ci = avail_idx[j];
			void const *ptr = MFCD::Retrieve(k_iconset_candidates[ci]);
			ST16_IControlView ic;
			uint32_t total = 0;
			int ok = 0;

			if (ptr) {
				const uint8_t *raw = (const uint8_t *)ptr;
				total = ST16_Read_LE32(raw + 8);
				if (total >= ST16_ICONTROL_SIZE) {
					ok = ST16_Parse_IControl(raw, (size_t)total, &ic);
				}
			}
			if (ok) {
				printf(
					"%2d %-12s %ux%u x%u\n",
					j + 1,
					k_iconset_candidates[ci],
					(unsigned)ic.width,
					(unsigned)ic.height,
					(unsigned)ic.count);
			} else {
				printf("%2d %-12s (bad header)\n", j + 1, k_iconset_candidates[ci]);
			}
		}
		printf("0 Back\nChoice: ");
		fflush(stdout);

		pick = st_read_decimal_choice();
		if (pick < 0) {
			return -1;
		}
		if (pick == 0) {
			return -1;
		}
		if (pick >= 1 && pick <= avail_count) {
			return avail_idx[pick - 1];
		}
		printf("Unknown option %d.\n", pick);
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
	total = ST16_Read_LE32(raw + 8);
	if (total < ST16_ICONTROL_SIZE || !ST16_Parse_IControl(raw, (size_t)total, &ic)) {
		return 0;
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
			total = ST16_Read_LE32(raw + 8);
		}
	}
	if (ST16_Is_Native(raw, (size_t)total)) {
		for (i = 0; i < (int)ic.count; ++i) {
			const int col = i % cols;
			const int row = i / cols;
			const int x = col * (int)ic.width;
			const int y = row * (int)ic.height;

			if (y + (int)ic.height > vp.Get_Height()) {
				break;
			}
			ST16_Blit_Stamp(&vp, (const IControl_Type *)raw, i, x, y);
		}
		used_st16 = 1;
	}

	if (!used_st16) {
		for (i = 0; i < (int)ic.count; ++i) {
			const uint8_t *icon = ST16_Standard_Icon_Ptr(raw, (size_t)total, i);
			const int col = i % cols;
			const int row = i / cols;
			const int x = col * (int)ic.width;
			const int y = row * (int)ic.height;

			if (!icon) {
				continue;
			}
			if (y + (int)ic.height > vp.Get_Height()) {
				break;
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
	int avail_idx[48];
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
		printf("(Need TEMPERAT.MIX beside tstcnc.tos.)\n");
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

		for (;;) {
			void const *icondata = MFCD::Retrieve(k_iconset_candidates[catalog_index]);
			ST16_IControlView ic;
			uint32_t total = 0;
			int parsed = 0;

			if (icondata) {
				const uint8_t *raw = (const uint8_t *)icondata;
				total = ST16_Read_LE32(raw + 8);
				if (total >= ST16_ICONTROL_SIZE) {
					parsed = ST16_Parse_IControl(raw, (size_t)total, &ic);
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

	st_hw_palette_write(saved_hw);
	Setscreen(old_log, old_phys, old_rez);
	SuperToUser(old_ssp);
	return 0;
}
