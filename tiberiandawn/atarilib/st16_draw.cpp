/*
 * st16_draw.cpp - ST16 iconset stamp blit (ST_Blit dispatch: HW or SW)
 */

#include "st16_draw.h"

#include "st16_iconset.h"
#include "st_blit.h"

#include <stdint.h>

static BOOL ST16_Blit_Planar_Rect(
	const uint8_t *planar,
	uint16_t planar_row_bytes,
	const uint8_t *mask,
	uint16_t mask_row_bytes,
	BOOL has_mask,
	int16_t src_x,
	int16_t src_y,
	uint8_t *dst_root,
	uint16_t dst_row_bytes,
	int16_t dst_x,
	int16_t dst_y,
	uint16_t blit_w,
	uint16_t blit_h)
{
	if (!planar || !dst_root || planar_row_bytes == 0 || dst_row_bytes == 0) {
		return FALSE;
	}
	if (blit_w == 0 || blit_h == 0) {
		return TRUE;
	}

	if (!has_mask) {
		return ST_Blit_Planar_Rect_Blit(
			planar,
			(int)planar_row_bytes,
			(int)src_x,
			(int)src_y,
			dst_root,
			(int)dst_row_bytes,
			(int)dst_x,
			(int)dst_y,
			(int)blit_w,
			(int)blit_h);
	}

	if (!mask || mask_row_bytes == 0) {
		return FALSE;
	}
	if (!ST_Blit_Mask_And_Planar_Rect(
			mask,
			(int)mask_row_bytes,
			(int)src_x,
			(int)src_y,
			dst_root,
			(int)dst_row_bytes,
			(int)dst_x,
			(int)dst_y,
			(int)blit_w,
			(int)blit_h)) {
		return FALSE;
	}
	return ST_Blit_Planar_Rect_Blit_Or(
		planar,
		(int)planar_row_bytes,
		(int)src_x,
		(int)src_y,
		dst_root,
		(int)dst_row_bytes,
		(int)dst_x,
		(int)dst_y,
		(int)blit_w,
		(int)blit_h);
}

BOOL ST16_Blit_Stamp(
	GraphicViewPortClass *vp,
	const IControl_Type *iconset,
	int16_t logical_icon,
	int16_t x_pixel,
	int16_t y_pixel)
{
	const uint8_t *const base = (const uint8_t *)iconset;
	/* All fields are native-endian after ST16_Convert_InPlace — no le*toh. */
	const uint32_t icons_off = (uint32_t)iconset->Icons;
	const uint32_t map_off = (uint32_t)iconset->Map;
	const uint32_t total_size = (uint32_t)iconset->Size;
	const uint16_t tile_w = (uint16_t)iconset->Width;
	const uint16_t tile_h = (uint16_t)iconset->Height;
	const uint16_t map_count = (uint16_t)iconset->Count;
	/* Chunk size/reserved were swapped; flags was already correct (little-endian 0 or 1). */
	const BOOL has_mask = (ST16_Chunk(iconset)->flags & ST16_FLAG_HAS_MASK) != 0;
	ST16_PlanarLayout layout;
	int16_t image_index;
	const uint8_t *planar;
	const uint8_t *mask;
	const uint8_t *map;
	GraphicBufferClass *dst_gb;
	uint8_t *dst_root;
	int16_t dst_x;
	int16_t dst_y;
	uint16_t clip_src_x;
	uint16_t clip_src_y;
	uint16_t clip_blit_w;
	uint16_t clip_blit_h;
	uint16_t dst_bpl;
	uint16_t dst_pw;
	uint16_t dst_ph;
	int16_t dx_abs;
	int16_t dy_abs;
	int16_t vp_w;
	int16_t vp_h;
	int16_t trim;

	(void)total_size;

	// Fast path for 24x24 tiles.
	if (tile_w == ST16_TILE_W && tile_h == ST16_TILE_H) {
		ST16_Fill_Planar_Layout_24x24(has_mask, &layout);
	} else {
		ST16_Compute_Planar_Layout((int)tile_w, (int)tile_h, has_mask, &layout);
	}
	if (layout.planar_stride <= 0 || layout.icon_stride <= 0) {
		return FALSE;
	}

	map = (map_off != 0) ? (base + map_off) : NULL;

	if (map) {
		if ((uint16_t)logical_icon >= map_count) {
			return TRUE;
		}
		image_index = (int16_t)map[logical_icon];
	} else {
		if ((uint16_t)logical_icon >= map_count) {
			return TRUE;
		}
		image_index = logical_icon;
	}

	planar = base + icons_off + (size_t)image_index * (size_t)layout.icon_stride;
	mask = NULL;
	if (has_mask) {
		mask = planar + (size_t)layout.planar_stride;
	}

	dst_x = x_pixel;
	dst_y = y_pixel;
	clip_src_x = 0;
	clip_src_y = 0;
	clip_blit_w = tile_w;
	clip_blit_h = tile_h;

	if (dst_x < 0) {
		clip_src_x = (uint16_t)(-dst_x);
		clip_blit_w = (uint16_t)(clip_blit_w - clip_src_x);
		dst_x = 0;
	}
	if (dst_y < 0) {
		clip_src_y = (uint16_t)(-dst_y);
		clip_blit_h = (uint16_t)(clip_blit_h - clip_src_y);
		dst_y = 0;
	}
	vp_w = (int16_t)vp->Get_Width();
	vp_h = (int16_t)vp->Get_Height();
	if ((int16_t)(dst_x + clip_blit_w) > vp_w) {
		clip_blit_w = (uint16_t)(vp_w - dst_x);
	}
	if ((int16_t)(dst_y + clip_blit_h) > vp_h) {
		clip_blit_h = (uint16_t)(vp_h - dst_y);
	}
	if (clip_blit_w == 0 || clip_blit_h == 0) {
		return TRUE;
	}

	dst_gb = vp->Get_Graphic_Buffer();
	if (!dst_gb) {
		return FALSE;
	}
	dst_root = (uint8_t *)dst_gb->Get_Buffer();
	if (!dst_root) {
		return FALSE;
	}
	{
		int const pitch = dst_gb->Get_Pitch();
		dst_bpl = (uint16_t)((pitch > 0) ? pitch : ST_Planar_Row_Bytes(dst_gb->Get_Width()));
	}
	dst_pw = (uint16_t)dst_gb->Get_Width();
	dst_ph = (uint16_t)dst_gb->Get_Height();
	dx_abs = (int16_t)(vp->Get_XPos() + dst_x);
	dy_abs = (int16_t)(vp->Get_YPos() + dst_y);
	if (dst_bpl == 0 || dst_pw == 0 || dst_ph == 0) {
		return FALSE;
	}

	if (dx_abs < 0) {
		trim = (int16_t)(-dx_abs);
		if (trim >= clip_blit_w) {
			return TRUE;
		}
		clip_src_x = (uint16_t)(clip_src_x + (uint16_t)trim);
		clip_blit_w = (uint16_t)(clip_blit_w - (uint16_t)trim);
		dx_abs = 0;
	}
	if (dy_abs < 0) {
		trim = (int16_t)(-dy_abs);
		if (trim >= clip_blit_h) {
			return TRUE;
		}
		clip_src_y = (uint16_t)(clip_src_y + (uint16_t)trim);
		clip_blit_h = (uint16_t)(clip_blit_h - (uint16_t)trim);
		dy_abs = 0;
	}
	if ((int16_t)(dx_abs + clip_blit_w) > (int16_t)dst_pw) {
		clip_blit_w = (uint16_t)(dst_pw - (uint16_t)dx_abs);
	}
	if ((int16_t)(dy_abs + clip_blit_h) > (int16_t)dst_ph) {
		clip_blit_h = (uint16_t)(dst_ph - (uint16_t)dy_abs);
	}
	if (clip_blit_w == 0 || clip_blit_h == 0) {
		return TRUE;
	}

	return ST16_Blit_Planar_Rect(
		planar,
		(uint16_t)layout.planar_row_bytes,
		mask,
		(uint16_t)layout.mask_row_bytes,
		has_mask,
		(int16_t)clip_src_x,
		(int16_t)clip_src_y,
		dst_root,
		dst_bpl,
		dx_abs,
		dy_abs,
		clip_blit_w,
		clip_blit_h);
}
