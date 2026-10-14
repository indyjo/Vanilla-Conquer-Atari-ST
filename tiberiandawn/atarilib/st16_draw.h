/*
 * st16_draw.h - Blit ST16 iconset stamps to a planar viewport
 */

#ifndef ST16_DRAW_H
#define ST16_DRAW_H

#include "function.h"
#include "gbuffer.h"
#include "st16_iconset.h"
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

/* Native unmasked 24×24 stamp plane 0, or NULL (masked, wrong size, empty slot). */
const uint8_t *ST16_Stamp24_Unmasked_Planar(const void *icondata, int logical_icon);

/*
 * One template's 24×24 unmasked ST16 iconset, filled at theater load.
 * icons/map are NULL when this template is not that kind of stamp.
 */
typedef struct ST16_Stamp24_Set
{
	const uint8_t *icons;
	const uint8_t *map;
	int16_t count;
} ST16_Stamp24_Set;

void ST16_Stamp24_Reset(void);
void ST16_Stamp24_Bind(int template_index, const void *icondata);
extern ST16_Stamp24_Set ST16_Stamp24_Templates[];

/* Plane 0 for a bound template, or NULL. template_index is not TEMPLATE_NONE. */
static inline const uint8_t *ST16_Stamp24_Planar(int template_index, int logical_icon)
{
	if ((unsigned)template_index >= (unsigned)TEMPLATE_COUNT) {
		return NULL;
	}
	ST16_Stamp24_Set const *const set = &ST16_Stamp24_Templates[template_index];
	if (!set->icons || logical_icon < 0 || logical_icon >= set->count) {
		return NULL;
	}
	int const image = set->map ? (int)set->map[logical_icon] : logical_icon;
	return set->icons + (size_t)image * (size_t)ST16_TILE_ICON_STRIDE;
}

#ifdef __cplusplus
}
#endif

#endif /* ST16_DRAW_H */
