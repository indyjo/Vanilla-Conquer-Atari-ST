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
void StFrameMeter_C2PBegin(void);
void StFrameMeter_C2PEnd(void);
void StFrameMeter_BlitBegin(void);
void StFrameMeter_BlitEnd(void);

typedef enum StFrameMeterMapPhase {
	ST_FM_MAP_PREP = 0,
	ST_FM_MAP_ICONS,
	ST_FM_MAP_LAYER_GROUND,
	ST_FM_MAP_LAYER_AIR,
	ST_FM_MAP_LAYER_TOP,
	ST_FM_MAP_SHADOW,
	ST_FM_MAP_PHASE_COUNT
} StFrameMeterMapPhase;

void StFrameMeter_MapPhaseBegin(StFrameMeterMapPhase phase);
void StFrameMeter_MapPhaseEnd(StFrameMeterMapPhase phase);

void StFrameMeter_Draw(GraphicViewPortClass *page, int width_factor);

#define ST_FRAME_BAR_FRAME_BEGIN() StFrameMeter_FrameBegin()
#define ST_FRAME_BAR_FRAME_END() StFrameMeter_FrameEnd()
#define ST_FRAME_BAR_RENDER_BEGIN() StFrameMeter_RenderBegin()
#define ST_FRAME_BAR_RENDER_END() StFrameMeter_RenderEnd()
#define ST_FRAME_BAR_LOGIC_BEGIN() StFrameMeter_LogicBegin()
#define ST_FRAME_BAR_LOGIC_END() StFrameMeter_LogicEnd()
#define ST_FRAME_BAR_THEME_BEGIN() StFrameMeter_ThemeBegin()
#define ST_FRAME_BAR_THEME_END() StFrameMeter_ThemeEnd()
/** Retired sub-bars under render timing; chunked-to-planar and blitter are no longer plotted here */
#define ST_FRAME_BAR_C2P_BEGIN() StFrameMeter_C2PBegin()
#define ST_FRAME_BAR_C2P_END() StFrameMeter_C2PEnd()
#define ST_FRAME_BAR_BLIT_BEGIN() StFrameMeter_BlitBegin()
#define ST_FRAME_BAR_BLIT_END() StFrameMeter_BlitEnd()
#define ST_FRAME_BAR_MAP_PREP_BEGIN() StFrameMeter_MapPhaseBegin(ST_FM_MAP_PREP)
#define ST_FRAME_BAR_MAP_PREP_END() StFrameMeter_MapPhaseEnd(ST_FM_MAP_PREP)
#define ST_FRAME_BAR_MAP_ICONS_BEGIN() StFrameMeter_MapPhaseBegin(ST_FM_MAP_ICONS)
#define ST_FRAME_BAR_MAP_ICONS_END() StFrameMeter_MapPhaseEnd(ST_FM_MAP_ICONS)
#define ST_FRAME_BAR_MAP_LAYER_BEGIN(layer_ix) \
	StFrameMeter_MapPhaseBegin((StFrameMeterMapPhase)((int)ST_FM_MAP_LAYER_GROUND + (layer_ix)))
#define ST_FRAME_BAR_MAP_LAYER_END(layer_ix) \
	StFrameMeter_MapPhaseEnd((StFrameMeterMapPhase)((int)ST_FM_MAP_LAYER_GROUND + (layer_ix)))
#define ST_FRAME_BAR_MAP_SHADOW_BEGIN() StFrameMeter_MapPhaseBegin(ST_FM_MAP_SHADOW)
#define ST_FRAME_BAR_MAP_SHADOW_END() StFrameMeter_MapPhaseEnd(ST_FM_MAP_SHADOW)

#else

#define ST_FRAME_BAR_FRAME_BEGIN() ((void)0)
#define ST_FRAME_BAR_FRAME_END() ((void)0)
#define ST_FRAME_BAR_RENDER_BEGIN() ((void)0)
#define ST_FRAME_BAR_RENDER_END() ((void)0)
#define ST_FRAME_BAR_LOGIC_BEGIN() ((void)0)
#define ST_FRAME_BAR_LOGIC_END() ((void)0)
#define ST_FRAME_BAR_THEME_BEGIN() ((void)0)
#define ST_FRAME_BAR_THEME_END() ((void)0)
#define ST_FRAME_BAR_C2P_BEGIN() ((void)0)
#define ST_FRAME_BAR_C2P_END() ((void)0)
#define ST_FRAME_BAR_BLIT_BEGIN() ((void)0)
#define ST_FRAME_BAR_BLIT_END() ((void)0)
#define ST_FRAME_BAR_MAP_PREP_BEGIN() ((void)0)
#define ST_FRAME_BAR_MAP_PREP_END() ((void)0)
#define ST_FRAME_BAR_MAP_ICONS_BEGIN() ((void)0)
#define ST_FRAME_BAR_MAP_ICONS_END() ((void)0)
#define ST_FRAME_BAR_MAP_LAYER_BEGIN(layer_ix) ((void)(layer_ix))
#define ST_FRAME_BAR_MAP_LAYER_END(layer_ix) ((void)(layer_ix))
#define ST_FRAME_BAR_MAP_SHADOW_BEGIN() ((void)0)
#define ST_FRAME_BAR_MAP_SHADOW_END() ((void)0)

static inline void StFrameMeter_Draw(GraphicViewPortClass *, int)
{
}

#endif /* ATARI_ST && ST_FRAME_BAR_PROFILE */

#endif /* ST_FRAME_METER_H */
