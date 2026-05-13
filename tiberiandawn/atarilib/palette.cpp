/*
 * palette.cpp - Palette functions for Atari ST/MiNT
 */

#include "palette.h"
#include "misc.h"
#include "st_temperat_palette.h"

#include <string.h>

#include "c2p.h"

extern unsigned char *GamePalette;

/* Current palette buffer - copy of current DAC register values */
/* Initialized to 255 (white) to match WIN32LIB behavior */
extern "C" unsigned char CurrentPalette[768] = {255};

static void Install_ST_Hardware_Palette_First16(const unsigned char *pal768)
{
    if (!pal768) return;
    St_HW_Palette_Write_First16_From_Logical_Pal6(ST_HW_PALETTE_REGS, pal768);
}

static void Apply_Palette_State(const unsigned char *pal768)
{
    for (int i = 0; i < 768; i++) {
        CurrentPalette[i] = pal768[i] & 63;
    }

    Install_ST_Hardware_Palette_First16(CurrentPalette);
}

static void Determine_Bump_Rate(const unsigned char *target_palette, unsigned int delay, short *ticks, short *rate)
{
    int diff = 0;

    for (int index = 0; index < 768; index++) {
        int gun1 = target_palette[index] & 63;
        int gun2 = CurrentPalette[index] & 63;
        int adiff = gun1 - gun2;
        if (adiff < 0) adiff = -adiff;
        if (adiff > diff) diff = adiff;
    }

    long t = ((long)(delay ? delay : 1U)) << 8;
    if (diff) {
        t /= diff;
        if (t > 0x7FFF) t = 0x7FFF;
    }
    *ticks = (short)t;

    int tp = *ticks;
    *rate = 1;
    while (*rate <= diff && *ticks < 256) {
        *ticks += tp;
        *rate += 1;
    }
}

static int Bump_Palette(const unsigned char *target_palette, unsigned int step)
{
    int changed = 0;
    unsigned char palette[768];
    memcpy(palette, CurrentPalette, sizeof(palette));

    for (int index = 0; index < 768; index++) {
        int gun1 = target_palette[index] & 63;
        int gun2 = palette[index] & 63;

        if (gun1 == gun2) {
            continue;
        }

        changed = 1;
        if (gun2 < gun1) {
            gun2 += (int)step;
            if (gun2 > gun1) gun2 = gun1;
        } else {
            gun2 -= (int)step;
            if (gun2 < gun1) gun2 = gun1;
        }

        palette[index] = (unsigned char)gun2;
    }

    if (changed) {
        Apply_Palette_State(palette);
    }

    return changed;
}

extern "C" void Set_Palette(void *palette)
{
    if (!palette) return;

    Apply_Palette_State((unsigned char *)palette);
}

void Palette_Debug_Fill_Index_Grid_Chunky(
	unsigned char *chunky,
	int width_pixels,
	int height_pixels,
	int row_stride_bytes)
{
	if (!chunky || width_pixels <= 0 || height_pixels <= 0)
		return;
	if (row_stride_bytes < width_pixels)
		return;

	const int grid_px = 16 * 8;
	if (width_pixels < grid_px || height_pixels < grid_px)
		return;

	const int x0 = (width_pixels - grid_px) / 2;
	const int y0 = (height_pixels - grid_px) / 2;

	for (int y = 0; y < height_pixels; ++y) {
		memset(chunky + (long)y * row_stride_bytes, 0, (size_t)width_pixels);
	}

	for (int gy = 0; gy < 16; ++gy) {
		for (int gx = 0; gx < 16; ++gx) {
			unsigned char c = (unsigned char)(gy * 16 + gx);
			for (int dy = 0; dy < 8; ++dy) {
				unsigned char *row = chunky + (long)(y0 + gy * 8 + dy) * row_stride_bytes;
				for (int dx = 0; dx < 8; ++dx) {
					row[x0 + gx * 8 + dx] = c;
				}
			}
		}
	}
}

void Palette_Debug_Draw_Index_Grid_To_Planar320(
	unsigned char *planar_base,
	int planar_row_bytes,
	const unsigned char *rgb768_preview)
{
	if (!planar_base)
		return;
	if (planar_row_bytes <= 0)
		return;

	if (rgb768_preview) {
		Set_Palette((void *)rgb768_preview);
	}

	const long plane_bytes = (long)planar_row_bytes * (long)ST_PLANAR_HEIGHT;
	if (plane_bytes > 0) {
		memset(planar_base, 0, (size_t)plane_bytes);
	}

	const int grid_px = 16 * 8;
	const int x0 = (ST_PLANAR_WIDTH - grid_px) / 2;
	const int y0 = (ST_PLANAR_HEIGHT - grid_px) / 2;

	for (int gy = 0; gy < 16; ++gy) {
		for (int gx = 0; gx < 16; ++gx) {
			unsigned char const pal_idx = (unsigned char)(gy * 16 + gx);
			C2P_Fill_Aligned8_Rect(
				(uint8_t *)planar_base,
				planar_row_bytes,
				ST_PLANAR_WIDTH,
				ST_PLANAR_HEIGHT,
				x0 + gx * 8,
				y0 + gy * 8,
				8,
				8,
				pal_idx);
		}
	}

	Wait_Vert_Blank();
	Wait_Vert_Blank();
}

void Fade_Palette_To(void *palette1, unsigned int delay, void (*callback)())
{
    if (!palette1) return;

    unsigned char *target_palette = (unsigned char *)palette1;
    /*
     * No longer pin C2P to the TEMPERAT weight set when fading to GamePalette:
     * the per-theater weight LUT is now installed in DisplayClass::Init_Theater
     * via Theater_Atari_TryInstallC2PWeights(), and forcing TEMPERAT here would
     * undo that for every Fade_Palette_To(GamePalette, ...) call (e.g. the
     * mission intro/outro fade chain in INIT.CPP).
     */

    short ticks_per_step = 0;
    short jump = 1;
    Determine_Bump_Rate(target_palette, delay, &ticks_per_step, &jump);

    int tick_accum = 0;
    while (Bump_Palette(target_palette, (unsigned int)jump)) {
        tick_accum += ticks_per_step;
        int wait_vbls = tick_accum >> 8;
        tick_accum &= 0xFF;

        for (int i = 0; i < wait_vbls; i++) {
            if (callback) {
                callback();
            }
            Wait_Vert_Blank();
        }

        if (callback) {
            callback();
        }
    }

    Set_Palette(target_palette);
    if (callback) {
        callback();
    }
}

enum { ST_HW_PALETTE_SNAPSHOT_COUNT = 16 };

static unsigned short s_st_hw_palette_snapshot[ST_HW_PALETTE_SNAPSHOT_COUNT];
static int s_st_hw_palette_snapshot_valid = 0;

extern "C" void Palette_ST_Capture_Hardware_State_Once(void)
{
	if (s_st_hw_palette_snapshot_valid) {
		return;
	}
	volatile unsigned short *const regs = ST_HW_PALETTE_REGS;
	for (int i = 0; i < ST_HW_PALETTE_SNAPSHOT_COUNT; i++) {
		s_st_hw_palette_snapshot[i] = regs[i];
	}
	s_st_hw_palette_snapshot_valid = 1;
}

extern "C" void Palette_ST_Restore_Hardware_State_And_Clear(void)
{
	if (!s_st_hw_palette_snapshot_valid) {
		return;
	}
	volatile unsigned short *const regs = ST_HW_PALETTE_REGS;
	for (int i = 0; i < ST_HW_PALETTE_SNAPSHOT_COUNT; i++) {
		regs[i] = s_st_hw_palette_snapshot[i];
	}
	s_st_hw_palette_snapshot_valid = 0;
}

