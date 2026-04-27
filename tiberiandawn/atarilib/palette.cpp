/*
 * palette.cpp - Palette functions for Atari ST/MiNT
 */

#include "palette.h"
#include "c2p.h"
#include "misc.h"
#include "st_temperat_palette.h"

#include <string.h>

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

void Fade_Palette_To(void *palette1, unsigned int delay, void (*callback)())
{
    if (!palette1) return;

    unsigned char *target_palette = (unsigned char *)palette1;
    if (target_palette == GamePalette) {
        C2P_Select_WeightSet(C2P_WEIGHTSET_TEMPERAT);
    }

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
