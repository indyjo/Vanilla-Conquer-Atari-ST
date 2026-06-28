/*
 * st16_draw.h - Blit ST16 iconset stamps to a planar viewport
 */

#ifndef ST16_DRAW_H
#define ST16_DRAW_H

#include "function.h"
#include "gbuffer.h"
#include "tile.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Blit one logical stamp from a native ST16 iconset onto a planar viewport.
 * Caller must route through Buffer_Draw_Stamp (or equivalent) so the viewport
 * graphic buffer is ST planar. Iconset must already be ST16 native (converted if needed).
 * Unmasked: HW blitter when HardwareFills, else CPU planar copy.
 * Masked: HW mask + OR blit (requires HardwareFills).
 * Returns TRUE if handled (including fully clipped or empty map slots).
 */
BOOL ST16_Blit_Stamp(
	GraphicViewPortClass *vp,
	const IControl_Type *iconset,
	int16_t logical_icon,
	int16_t x_pixel,
	int16_t y_pixel);

#ifdef __cplusplus
}
#endif

#endif /* ST16_DRAW_H */
