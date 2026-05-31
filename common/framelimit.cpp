#include "framelimit.h"
#include "wwmouse.h"
#include "settings.h"
#include <chrono>

#ifdef ATARI_ST
enum
{
    ST_HZ200_ADDR = 0x4BA,
    ST_HZ200_TICK_US = 1000000 / 200,
};
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include "mssleep.h"

extern WWMouseClass* WWMouse;

#ifdef NEW_VIDEO_BUILD
void Video_Render_Frame();
#endif

#ifdef ATARI_ST
static unsigned long St_Read_Hz200(void)
{
    return *(volatile unsigned long *)ST_HZ200_ADDR;
}

static long long St_Now_Us(void)
{
    return (long long)St_Read_Hz200() * (long long)ST_HZ200_TICK_US;
}
#endif

void Frame_Limiter(FrameLimitFlags flags)
{
#ifdef ATARI_ST
    static long long frame_start_us = 0;
    static bool frame_start_set = false;
#else
    static auto frame_start = std::chrono::steady_clock::now();
#endif
#ifdef NEW_VIDEO_BUILD
    static auto render_avg = 0;

    auto render_start = std::chrono::steady_clock::now();
    auto render_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - render_start).count();

    if (!(flags & FrameLimitFlags::FL_FORCE_RENDER) && render_remaining > render_avg) {
        if (!(flags & FrameLimitFlags::FL_NO_BLOCK)) {
            ms_sleep(unsigned(render_remaining));
        } else {
            ms_sleep(1); // Unconditionally yield for minimum time.
        }
        return;
    }

    Video_Render_Frame();

    auto render_end = std::chrono::steady_clock::now();
    auto render_time = std::chrono::duration_cast<std::chrono::milliseconds>(render_end - render_start).count();

    // keep up some average so we have an idea if we need to skip a frame or not
    render_avg = (render_avg + render_time) / 2;
#endif

    if (Settings.Video.FrameLimit > 0 && !(flags & FrameLimitFlags::FL_NO_BLOCK)) {
        unsigned int const min_frame_time = 1000000 / Settings.Video.FrameLimit;
#ifdef ATARI_ST
        long long const frame_end_us = St_Now_Us();
        if (!frame_start_set) {
            frame_start_us = frame_end_us;
            frame_start_set = true;
        }
        long long const cur_frame_time = frame_end_us - frame_start_us;
        if (cur_frame_time < (long long)min_frame_time) {
            frame_start_us += min_frame_time;
            us_sleep(min_frame_time - (unsigned int)cur_frame_time);
        } else {
            frame_start_us = frame_end_us;
        }
#else
#ifdef NEW_VIDEO_BUILD
        auto frame_end = render_end;
#else
        auto frame_end = std::chrono::steady_clock::now();
#endif
        auto cur_frame_time = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        if (cur_frame_time < min_frame_time) {
            frame_start += std::chrono::microseconds{min_frame_time};
            us_sleep(min_frame_time - cur_frame_time);
        } else {
            frame_start = frame_end;
        }
#endif
    }
}
