#ifdef ATARI_ST

#include "st_playback_timing.h"

#include "function.h"
#include "msgbox.h"

#include <cstdio>

enum { ST_HZ200_ADDR = 0x4BA, ST_HZ200_TICKS_PER_SEC = 200 };

static unsigned long St_Read_Hz200(void)
{
    return *(volatile unsigned long*)ST_HZ200_ADDR;
}

static bool s_active = false;
static unsigned long s_hz200_start = 0;
static int s_frame_start = 0;

extern "C" ST_PLAYBACK_TIMING_HOOK void HatariProfileStart(void)
{
    /* Keep a real call frame for Hatari even if a caller is LTO'd. */
    asm volatile("" ::: "memory");
    s_hz200_start = St_Read_Hz200();
    s_frame_start = Frame;
    s_active = true;
}

void StPlaybackTiming_Start(void)
{
    HatariProfileStart();
}

extern "C" ST_PLAYBACK_TIMING_HOOK void HatariProfileEnd(void)
{
    if (!s_active) {
        return;
    }

    s_active = false;

    const unsigned long hz200_end = St_Read_Hz200();
    const unsigned long ticks = hz200_end - s_hz200_start;
    const int frames = Frame - s_frame_start;

    const unsigned long total_hundredths = (ticks * 100UL) / (unsigned long)ST_HZ200_TICKS_PER_SEC;
    const unsigned minutes = (unsigned)(total_hundredths / 6000UL);
    const unsigned seconds = (unsigned)((total_hundredths / 100UL) % 60UL);
    const unsigned hundredths = (unsigned)(total_hundredths % 100UL);

    char msg[96];
    double fps = 0.0;
    if (ticks > 0UL) {
        fps = (double)frames * (double)ST_HZ200_TICKS_PER_SEC / (double)ticks;
    }

    printf("frames: %d\n", frames);
    printf("ticks: %lu\n", ticks);
    printf("time: %u:%02u.%02u\n", minutes, seconds, hundredths);
    printf("fps: %.4f\n", fps);

    snprintf(msg,
             sizeof(msg),
             "frames: %d\rticks: %lu\rtime: %u:%02u.%02u\rfps: %.4f",
             frames,
             ticks,
             minutes,
             seconds,
             hundredths,
             fps);

    Set_Palette(GamePalette);
    WWMessageBox().Process(msg, TXT_OK);
}

void StPlaybackTiming_EndAndPrint(void)
{
    HatariProfileEnd();
}

#endif /* ATARI_ST */
