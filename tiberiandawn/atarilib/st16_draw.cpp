/*
 * st16_draw.cpp - ST16 iconset stamp blit (HW when enabled, CPU fallback)
 */

#include "st16_draw.h"

#include "st16_iconset.h"
#include "st_blitter_blit.h"
#include "endianness.h"

#include <stdint.h>

static inline BOOL ST16_Read_Chunk_Has_Mask(const IControl_Type *iconset)
{
	const ST16_Chunk_Type *const chunk =
		(const ST16_Chunk_Type *)((const char *)iconset + ST16_CHUNK_OFFSET);

	if (le32toh(chunk->magic) != ST16_MAGIC) {
		return FALSE;
	}
	if (le32toh(chunk->size) != ST16_PAYLOAD_SIZE) {
		return FALSE;
	}
	if (le16toh(chunk->reserved) != 0) {
		return FALSE;
	}
	return (le16toh(chunk->flags) & ST16_FLAG_HAS_MASK) != 0;
}

static uint8_t ST16_CPU_Get_Pixel(
	const uint8_t *base,
	uint16_t row_bytes,
	uint16_t width_px,
	uint16_t height_px,
	int16_t x,
	int16_t y)
{
	if (!base || x < 0 || y < 0 || x >= width_px || y >= height_px || row_bytes == 0) {
		return 0;
	}

	const uint8_t *p = base + (uint16_t)y * row_bytes + (uint16_t)((x >> 4) * 8 + ((x >> 3) & 1));
	const int bitnum = 7 - (x & 7);
	const uint8_t mask = (uint8_t)(1u << bitnum);
	uint8_t c = 0;

	for (int pl = 0; pl < 4; ++pl) {
		if (p[pl * 2] & mask) {
			c |= (uint8_t)(1u << pl);
		}
	}
	return c;
}

static void ST16_CPU_Put_Pixel(
	uint8_t *base,
	uint16_t row_bytes,
	uint16_t width_px,
	uint16_t height_px,
	int16_t x,
	int16_t y,
	uint8_t color4)
{
	if (!base || x < 0 || y < 0 || x >= width_px || y >= height_px || row_bytes == 0) {
		return;
	}

	uint8_t *p = base + (uint16_t)y * row_bytes + (uint16_t)((x >> 4) * 8 + ((x >> 3) & 1));
	const int bitnum = 7 - (x & 7);
	const uint8_t mask = (uint8_t)(1u << bitnum);
	const uint8_t c = (uint8_t)(color4 & 15u);

	for (int pl = 0; pl < 4; ++pl) {
		uint8_t *pb = p + pl * 2;
		if (c & (uint8_t)(1u << pl)) {
			*pb |= mask;
		} else {
			*pb &= (uint8_t)~mask;
		}
	}
}

static BOOL ST16_CPU_Blit_Planar_Rect(
	const uint8_t *src,
	uint16_t src_row_bytes,
	uint16_t src_width_px,
	uint16_t src_height_px,
	int16_t src_x,
	int16_t src_y,
	uint8_t *dst,
	uint16_t dst_row_bytes,
	uint16_t dst_width_px,
	uint16_t dst_height_px,
	int16_t dst_x,
	int16_t dst_y,
	uint16_t blit_w,
	uint16_t blit_h)
{
	if (!src || !dst || src_row_bytes == 0 || dst_row_bytes == 0) {
		return FALSE;
	}
	if (blit_w == 0 || blit_h == 0) {
		return TRUE;
	}

	for (uint16_t yy = 0; yy < blit_h; ++yy) {
		const int16_t sy = src_y + (int16_t)yy;
		const int16_t dy = dst_y + (int16_t)yy;
		for (uint16_t xx = 0; xx < blit_w; ++xx) {
			const int16_t sx = src_x + (int16_t)xx;
			const int16_t dx = dst_x + (int16_t)xx;
			const uint8_t c =
				ST16_CPU_Get_Pixel(src, src_row_bytes, src_width_px, src_height_px, sx, sy);
			ST16_CPU_Put_Pixel(dst, dst_row_bytes, dst_width_px, dst_height_px, dx, dy, c);
		}
	}
	return TRUE;
}

static BOOL ST16_CPU_Mask_Preserve(const uint8_t *mask_row, int16_t x)
{
	const uint16_t word_ix = (uint16_t)(x >> 4);
	const int bit = 15 - (x & 15);
	const uint16_t word = *(const uint16_t *)(mask_row + word_ix * 2);

	return (word >> bit) & 1;
}

static BOOL ST16_CPU_Blit_Masked_Planar_Rect(
	const uint8_t *planar,
	uint16_t planar_row_bytes,
	uint16_t planar_width_px,
	uint16_t planar_height_px,
	const uint8_t *mask,
	uint16_t mask_row_bytes,
	int16_t src_x,
	int16_t src_y,
	uint8_t *dst,
	uint16_t dst_row_bytes,
	uint16_t dst_width_px,
	uint16_t dst_height_px,
	int16_t dst_x,
	int16_t dst_y,
	uint16_t blit_w,
	uint16_t blit_h)
{
	if (!planar || !mask || !dst || planar_row_bytes == 0 || mask_row_bytes == 0 || dst_row_bytes == 0) {
		return FALSE;
	}
	if (blit_w == 0 || blit_h == 0) {
		return TRUE;
	}

	for (uint16_t yy = 0; yy < blit_h; ++yy) {
		const int16_t sy = src_y + (int16_t)yy;
		const int16_t dy = dst_y + (int16_t)yy;
		const uint8_t *mask_row = mask + (uint16_t)sy * mask_row_bytes;

		for (uint16_t xx = 0; xx < blit_w; ++xx) {
			const int16_t sx = src_x + (int16_t)xx;
			const int16_t dx = dst_x + (int16_t)xx;

			if (ST16_CPU_Mask_Preserve(mask_row, sx)) {
				continue;
			}
			const uint8_t c =
				ST16_CPU_Get_Pixel(planar, planar_row_bytes, planar_width_px, planar_height_px, sx, sy);
			ST16_CPU_Put_Pixel(dst, dst_row_bytes, dst_width_px, dst_height_px, dx, dy, c);
		}
	}
	return TRUE;
}

static BOOL ST16_Blit_Planar_Rect(
	const uint8_t *planar,
	uint16_t planar_row_bytes,
	uint16_t planar_width_px,
	uint16_t planar_height_px,
	const uint8_t *mask,
	uint16_t mask_row_bytes,
	BOOL has_mask,
	int16_t src_x,
	int16_t src_y,
	uint8_t *dst_root,
	uint16_t dst_row_bytes,
	uint16_t dst_width_px,
	uint16_t dst_height_px,
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
		if (AllowHardwareBlitFills
			&& ST_Blitter_Planar_Rect_Blit(
				planar,
				(int)planar_row_bytes,
				(int)src_x,
				(int)src_y,
				dst_root,
				(int)dst_row_bytes,
				(int)dst_x,
				(int)dst_y,
				(int)blit_w,
				(int)blit_h)) {
			return TRUE;
		}
		return ST16_CPU_Blit_Planar_Rect(
			planar,
			planar_row_bytes,
			planar_width_px,
			planar_height_px,
			src_x,
			src_y,
			dst_root,
			dst_row_bytes,
			dst_width_px,
			dst_height_px,
			dst_x,
			dst_y,
			blit_w,
			blit_h);
	}

	if (!mask || mask_row_bytes == 0) {
		return FALSE;
	}
	if (AllowHardwareBlitFills
		&& ST_Blitter_Mask_And_Planar_Rect(
			mask,
			(int)mask_row_bytes,
			(int)src_x,
			(int)src_y,
			dst_root,
			(int)dst_row_bytes,
			(int)dst_x,
			(int)dst_y,
			(int)blit_w,
			(int)blit_h)
		&& ST_Blitter_Planar_Rect_Blit_Or(
			planar,
			(int)planar_row_bytes,
			(int)src_x,
			(int)src_y,
			dst_root,
			(int)dst_row_bytes,
			(int)dst_x,
			(int)dst_y,
			(int)blit_w,
			(int)blit_h)) {
		return TRUE;
	}
	return ST16_CPU_Blit_Masked_Planar_Rect(
		planar,
		planar_row_bytes,
		planar_width_px,
		planar_height_px,
		mask,
		mask_row_bytes,
		src_x,
		src_y,
		dst_root,
		dst_row_bytes,
		dst_width_px,
		dst_height_px,
		dst_x,
		dst_y,
		blit_w,
		blit_h);
}

BOOL ST16_Blit_Stamp(
	GraphicViewPortClass *vp,
	const IControl_Type *iconset,
	int16_t logical_icon,
	int16_t x_pixel,
	int16_t y_pixel)
{
	const uint8_t *const base = (const uint8_t *)iconset;
	const uint32_t icons_off = (uint32_t)le32toh(iconset->Icons);
	const uint32_t map_off = (uint32_t)le32toh(iconset->Map);
	const uint32_t total_size = (uint32_t)le32toh(iconset->Size);
	const uint16_t tile_w = le16toh(iconset->Width);
	const uint16_t tile_h = le16toh(iconset->Height);
	const uint16_t map_count = le16toh(iconset->Count);
	const BOOL has_mask = ST16_Read_Chunk_Has_Mask(iconset);
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

	if (!vp || !iconset || logical_icon < 0 || tile_w == 0 || tile_h == 0) {
		return FALSE;
	}
	if (icons_off < ST16_ICONS_V1) {
		return FALSE;
	}

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

	if (image_index < 0 || (uint8_t)image_index == 0xFF) {
		return TRUE;
	}

	planar = base + icons_off + (size_t)image_index * (size_t)layout.icon_stride;
	if (total_size != 0
		&& icons_off + (size_t)image_index * (size_t)layout.icon_stride
			+ (size_t)layout.icon_stride
			> (size_t)total_size) {
		return FALSE;
	}
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
		(uint16_t)layout.planar_w,
		(uint16_t)layout.planar_h,
		mask,
		(uint16_t)layout.mask_row_bytes,
		has_mask,
		(int16_t)clip_src_x,
		(int16_t)clip_src_y,
		dst_root,
		dst_bpl,
		dst_pw,
		dst_ph,
		dx_abs,
		dy_abs,
		clip_blit_w,
		clip_blit_h);
}
