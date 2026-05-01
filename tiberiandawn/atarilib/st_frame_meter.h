/*
 * ST frame profiler: ATARI_ST + ST_FRAME_BAR_PROFILE only.
 * Timings in _hz200 ticks (200 Hz, longword at OS address 0x4BA).
 */
#ifndef ST_FRAME_METER_H
#define ST_FRAME_METER_H

class GraphicViewPortClass;

#if defined(ATARI_ST) && defined(ST_FRAME_BAR_PROFILE)

void StFrameMeter_FrameBegin(void);
void StFrameMeter_FrameEnd(void);

void StFrameMeter_RenderBegin(void);
void StFrameMeter_RenderEnd(void);

void StFrameMeter_LogicBegin(void);
void StFrameMeter_LogicEnd(void);

void StFrameMeter_ThemeBegin(void);
void StFrameMeter_ThemeEnd(void);

void StFrameMeter_Draw(GraphicViewPortClass *page, int width_factor);

#define ST_FRAME_BAR_FRAME_BEGIN() StFrameMeter_FrameBegin()
#define ST_FRAME_BAR_FRAME_END() StFrameMeter_FrameEnd()
#define ST_FRAME_BAR_RENDER_BEGIN() StFrameMeter_RenderBegin()
#define ST_FRAME_BAR_RENDER_END() StFrameMeter_RenderEnd()
#define ST_FRAME_BAR_LOGIC_BEGIN() StFrameMeter_LogicBegin()
#define ST_FRAME_BAR_LOGIC_END() StFrameMeter_LogicEnd()
#define ST_FRAME_BAR_THEME_BEGIN() StFrameMeter_ThemeBegin()
#define ST_FRAME_BAR_THEME_END() StFrameMeter_ThemeEnd()

#else

#define ST_FRAME_BAR_FRAME_BEGIN() ((void)0)
#define ST_FRAME_BAR_FRAME_END() ((void)0)
#define ST_FRAME_BAR_RENDER_BEGIN() ((void)0)
#define ST_FRAME_BAR_RENDER_END() ((void)0)
#define ST_FRAME_BAR_LOGIC_BEGIN() ((void)0)
#define ST_FRAME_BAR_LOGIC_END() ((void)0)
#define ST_FRAME_BAR_THEME_BEGIN() ((void)0)
#define ST_FRAME_BAR_THEME_END() ((void)0)

static inline void StFrameMeter_Draw(GraphicViewPortClass *, int)
{
}

#endif /* ATARI_ST && ST_FRAME_BAR_PROFILE */

#endif /* ST_FRAME_METER_H */
