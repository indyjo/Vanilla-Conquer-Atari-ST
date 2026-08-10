/*
 * st16_iconset.cpp - ST16 ICN iconset layout helpers
 */

#include "st16_iconset.h"

#include "c2p.h"

#include <string.h>

typedef char ST16_IControl_Size_Check[(sizeof(IControl_Type) == ST16_ICONTROL_SIZE) ? 1 : -1];
typedef char ST16_Chunk_Offset_Check[(ST16_CHUNK_OFFSET == sizeof(IControl_Type)) ? 1 : -1];

uint16_t ST16_Read_LE16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t ST16_Read_LE32(const uint8_t *p)
{
	return (uint32_t)p[0]
		| ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16)
		| ((uint32_t)p[3] << 24);
}

uint16_t ST16_Read_BE16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t ST16_Read_BE32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24)
		| ((uint32_t)p[1] << 16)
		| ((uint32_t)p[2] << 8)
		| (uint32_t)p[3];
}

void ST16_Write_LE16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

void ST16_Write_LE32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void ST16_Write_BE16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)((v >> 8) & 0xFFu);
	p[1] = (uint8_t)(v & 0xFFu);
}

void ST16_Write_BE32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)((v >> 24) & 0xFFu);
	p[1] = (uint8_t)((v >> 16) & 0xFFu);
	p[2] = (uint8_t)((v >> 8) & 0xFFu);
	p[3] = (uint8_t)(v & 0xFFu);
}

BOOL ST16_Chunk_Valid(const uint8_t *base, size_t blob_size, uint16_t *flags_out)
{
	if (!base || blob_size < ST16_ICONTROL_SIZE + ST16_CHUNK_TOTAL) {
		return FALSE;
	}
	if (ST16_Read_LE32(base + ST16_CHUNK_OFFSET) != ST16_MAGIC) {
		return FALSE;
	}
	if (ST16_Read_LE32(base + ST16_CHUNK_OFFSET + 4) != ST16_PAYLOAD_SIZE) {
		return FALSE;
	}
	if (ST16_Read_LE16(base + ST16_CHUNK_OFFSET + 10) != 0) {
		return FALSE;
	}
	if (flags_out) {
		*flags_out = ST16_Read_LE16(base + ST16_CHUNK_OFFSET + 8);
	}
	return TRUE;
}

BOOL ST16_Is_Planar_Ready(const IControl_Type *ic)
{
	if (!ic) {
		return FALSE;
	}
	return *(const uint32_t *)((const uint8_t *)ic + ST16_CHUNK_OFFSET) == ST16_MAGIC_NATIVE;
}

BOOL ST16_Load_Blit_Context(const IControl_Type *ic, ST16_Blit_Context *ctx)
{
	const uint8_t *base;
	uint16_t flags;

	if (!ic || !ctx || !ST16_Is_Planar_Ready(ic)) {
		return FALSE;
	}

	base = (const uint8_t *)ic;
	if ((uint32_t)ic->Icons == ST16_ICONS_V1) {
		ST16_IControlView_From_Struct(ic, &ctx->view);
		flags = ST16_Chunk(ic)->flags;
		ctx->native_hdr = TRUE;
	} else {
		ctx->view.size = ST16_Read_LE32(base + 8);
		if (!ST16_Parse_IControl(base, (size_t)ctx->view.size, &ctx->view)) {
			return FALSE;
		}
		if (!ST16_Chunk_Valid(base, (size_t)ctx->view.size, &flags)) {
			return FALSE;
		}
		ctx->native_hdr = FALSE;
	}
	ctx->has_mask = (flags & ST16_FLAG_HAS_MASK) != 0;
	return TRUE;
}

BOOL ST16_Parse_IControl(const uint8_t *base, size_t blob_size, ST16_IControlView *out)
{
	if (!base || !out || blob_size < ST16_ICONTROL_SIZE) {
		return FALSE;
	}

	out->width = ST16_Read_LE16(base + 0);
	out->height = ST16_Read_LE16(base + 2);
	out->count = ST16_Read_LE16(base + 4);
	out->size = ST16_Read_LE32(base + 8);
	out->icons_off = ST16_Read_LE32(base + 12);
	out->transflag_off = ST16_Read_LE32(base + 24);
	out->map_off = ST16_Read_LE32(base + 28);

	if (out->width <= 0 || out->height <= 0 || out->width > 128 || out->height > 128) {
		return FALSE;
	}
	if (out->count <= 0) {
		return FALSE;
	}
	if (out->icons_off == 0) {
		return FALSE;
	}
	return TRUE;
}

BOOL ST16_Iconset_Uses_Mask(const uint8_t *base, size_t blob_size)
{
	ST16_IControlView ic;
	size_t icon_bytes;
	size_t data_end;
	size_t i;

	if (!base || !ST16_Parse_IControl(base, blob_size, &ic)) {
		return TRUE;
	}
	if (ic.transflag_off == 0) {
		return FALSE;
	}

	icon_bytes = (size_t)ic.width * (size_t)ic.height;
	if (icon_bytes == 0) {
		return TRUE;
	}
	{
		size_t image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
		if (image_count == 0) {
			return TRUE;
		}
		data_end = (size_t)ic.icons_off + image_count * icon_bytes;
	}
	{
		size_t const trans_bytes = ST16_Transflag_Byte_Count(base, blob_size, &ic);

		if (ic.transflag_off < data_end || trans_bytes == 0
			|| (size_t)ic.transflag_off + trans_bytes > blob_size) {
			return TRUE;
		}

		for (i = 0; i < trans_bytes; ++i) {
			if (base[ic.transflag_off + i] != 0) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

BOOL ST16_Is_Standard(const uint8_t *base, size_t blob_size)
{
	ST16_IControlView ic;

	if (!ST16_Parse_IControl(base, blob_size, &ic)) {
		return FALSE;
	}
	return ic.icons_off == ST16_ICONS_STANDARD;
}

BOOL ST16_Is_Native(const uint8_t *base, size_t blob_size)
{
	const IControl_Type *ic = (const IControl_Type *)base;

	if (!base || blob_size < ST16_ICONTROL_SIZE) {
		return FALSE;
	}
	return ST16_Has_Native_Chunk(ic) && ST16_Validate_Native(ic, blob_size);
}

BOOL ST16_Read_Chunk_Flags(const uint8_t *base, size_t blob_size, uint16_t *flags_out)
{
	const IControl_Type *ic = (const IControl_Type *)base;

	if (!base || blob_size < ST16_ICONTROL_SIZE + ST16_CHUNK_TOTAL) {
		return FALSE;
	}
	if (ST16_Has_Native_Chunk(ic)) {
		const ST16_Chunk_Type *chunk = ST16_Chunk(ic);

		if (chunk->size != ST16_PAYLOAD_SIZE || chunk->reserved != 0) {
			return FALSE;
		}
		if (flags_out) {
			*flags_out = chunk->flags;
		}
		return TRUE;
	}
	return ST16_Chunk_Valid(base, blob_size, flags_out);
}

void ST16_Compute_Planar_Layout(int width, int height, BOOL has_mask, ST16_PlanarLayout *out)
{
	if (!out) {
		return;
	}

	if (width == ST16_TILE_W && height == ST16_TILE_H) {
		ST16_Fill_Planar_Layout_24x24(has_mask, out);
		return;
	}

	memset(out, 0, sizeof(*out));
	if (width <= 0 || height <= 0) {
		return;
	}

	out->planar_w = (width + 15) & ~15;
	out->planar_h = height;
	out->planar_row_bytes = (out->planar_w >> 4) * 8;
	out->planar_stride = out->planar_row_bytes * out->planar_h;
	out->icon_stride = out->planar_stride;
	if (has_mask) {
		out->mask_row_bytes = (out->planar_w >> 4) * 2;
		out->mask_stride = out->mask_row_bytes * out->planar_h;
		out->icon_stride += out->mask_stride;
	}
}

void ST16_Clear_Planar_Icon_Padding(
	uint8_t *planar_icon,
	const ST16_PlanarLayout *layout,
	int tile_w,
	int tile_h)
{
	int px;
	int py;

	if (!planar_icon || !layout || tile_w <= 0 || tile_h <= 0) {
		return;
	}
	if (layout->planar_row_bytes <= 0 || layout->planar_w <= tile_w) {
		return;
	}

	for (py = 0; py < tile_h; ++py) {
		for (px = tile_w; px < layout->planar_w; ++px) {
			ST_Planar_PutPixel(
				planar_icon,
				layout->planar_row_bytes,
				layout->planar_w,
				layout->planar_h,
				px,
				py,
				0);
		}
	}
}

size_t ST16_Planar_Icons_Bytes(
	const uint8_t *base,
	size_t blob_size,
	const ST16_IControlView *ic,
	const ST16_PlanarLayout *layout)
{
	size_t image_count;

	if (!ic || !layout || layout->planar_stride <= 0) {
		return 0;
	}
	image_count = ST16_Icon_Image_Count(base, blob_size, ic);
	if (image_count == 0) {
		return 0;
	}
	return image_count * (size_t)layout->planar_stride;
}

size_t ST16_Icon_Image_Count(const uint8_t *base, size_t blob_size, const ST16_IControlView *ic)
{
	size_t chunky_stride;
	size_t icons_bytes;
	size_t per_image;

	if (!ic) {
		return 0;
	}

	if (ic->icons_off == 0) {
		return 0;
	}

	if (base && blob_size >= ST16_ICONTROL_SIZE && ST16_Has_Native_Chunk((const IControl_Type *)base)) {
		ST16_IControlView native_ic;
		ST16_PlanarLayout layout;
		const ST16_Chunk_Type *chunk = ST16_Chunk((const IControl_Type *)base);
		uint16_t flags;

		ST16_IControlView_From_Struct((const IControl_Type *)base, &native_ic);
		ic = &native_ic;

		if (chunk->size != ST16_PAYLOAD_SIZE || chunk->reserved != 0) {
			return 0;
		}
		flags = chunk->flags;
		ST16_Compute_Planar_Layout(
			(int)ic->width,
			(int)ic->height,
			(flags & ST16_FLAG_HAS_MASK) ? TRUE : FALSE,
			&layout);
		if (layout.planar_stride <= 0 || layout.icon_stride <= 0) {
			return 0;
		}
		per_image = (size_t)layout.icon_stride;

		if (ic->map_off > ic->icons_off) {
			if ((size_t)ic->map_off > blob_size) {
				return 0;
			}
			icons_bytes = (size_t)ic->map_off - (size_t)ic->icons_off;
			if (icons_bytes >= per_image && icons_bytes % per_image == 0) {
				return icons_bytes / per_image;
			}
		}

		if (ic->transflag_off > ic->icons_off
			&& (ic->map_off == 0 || ic->transflag_off <= ic->map_off)) {
			if ((size_t)ic->transflag_off > blob_size) {
				return 0;
			}
			icons_bytes = (size_t)ic->transflag_off - (size_t)ic->icons_off;
			if (icons_bytes >= per_image && icons_bytes % per_image == 0) {
				return icons_bytes / per_image;
			}
		}

		return (size_t)ic->count;
	}

	chunky_stride = (size_t)ic->width * (size_t)ic->height;
	if (chunky_stride == 0) {
		return 0;
	}

	if (ic->map_off > ic->icons_off) {
		if ((size_t)ic->map_off > blob_size) {
			return 0;
		}
		icons_bytes = (size_t)ic->map_off - (size_t)ic->icons_off;
		if (icons_bytes >= chunky_stride && icons_bytes % chunky_stride == 0) {
			return icons_bytes / chunky_stride;
		}
	}

	if (ic->transflag_off > ic->icons_off
		&& (ic->map_off == 0 || ic->transflag_off <= ic->map_off)) {
		if ((size_t)ic->transflag_off > blob_size) {
			return 0;
		}
		icons_bytes = (size_t)ic->transflag_off - (size_t)ic->icons_off;
		if (icons_bytes >= chunky_stride && icons_bytes % chunky_stride == 0) {
			return icons_bytes / chunky_stride;
		}
	}

	return (size_t)ic->count;
}

size_t ST16_Transflag_Byte_Count(
	const uint8_t *base,
	size_t blob_size,
	const ST16_IControlView *ic)
{
	size_t end;

	if (!ic || ic->transflag_off == 0 || ic->transflag_off >= blob_size) {
		return 0;
	}

	if (ic->map_off != 0 && ic->map_off > ic->transflag_off) {
		end = (size_t)ic->map_off;
	} else {
		end = blob_size;
		if (ic->size != 0 && (size_t)ic->size < end) {
			end = (size_t)ic->size;
		}
	}

	if (end <= (size_t)ic->transflag_off) {
		return 0;
	}
	(void)base;
	return end - (size_t)ic->transflag_off;
}

static void ST16_Mask_Flush_Run(uint8_t *mask_row, int word_ix, int cols, uint16_t accum)
{
	uint16_t word;

	if (cols <= 0 || !mask_row) {
		return;
	}
	word = (cols >= 16)
		? accum
		: (uint16_t)(((uint16_t)accum << (16 - cols)) | (uint16_t)(0xFFFFu >> cols));
	/* Mask plane words are native/big-endian (same as ST16 header after offline convert).
	 * Host remix runs on LE; a raw uint16_t store would byte-swap pad bits into x16..23. */
	ST16_Write_BE16(mask_row + word_ix * 2, word);
}

void ST16_Build_Mask_From_Chunky(
	const uint8_t *chunky,
	int width,
	int height,
	BOOL per_pixel_trans,
	uint8_t *mask_out,
	const ST16_PlanarLayout *layout)
{
	int py;
	int px;

	if (!chunky || !mask_out || !layout || width <= 0 || height <= 0) {
		return;
	}
	if (layout->mask_row_bytes <= 0 || layout->mask_stride <= 0) {
		return;
	}

	for (py = 0; py < height; ++py) {
		uint8_t *mask_row = mask_out + (size_t)py * (size_t)layout->mask_row_bytes;
		uint16_t mask_acc = 0;
		int mask_run = 0;
		int mask_word_ix = 0;

		for (px = 0; px < layout->planar_w; ++px) {
			BOOL preserve = TRUE;

			if (px < width) {
				const uint8_t raw = chunky[(size_t)py * (size_t)width + (size_t)px];
				if (!per_pixel_trans || raw != 0) {
					preserve = FALSE;
				}
			}

			mask_acc = (uint16_t)((mask_acc << 1) | (preserve ? 1u : 0u));
			mask_run++;
			if (mask_run == 16) {
				ST16_Mask_Flush_Run(mask_row, mask_word_ix, 16, mask_acc);
				mask_acc = 0;
				mask_run = 0;
				mask_word_ix++;
			}
		}
		if (mask_run > 0) {
			ST16_Mask_Flush_Run(mask_row, mask_word_ix, mask_run, mask_acc);
		}
	}
}

size_t ST16_Mask_Icons_Bytes(
	const uint8_t *base,
	size_t blob_size,
	const ST16_IControlView *ic,
	const ST16_PlanarLayout *layout)
{
	size_t image_count;

	if (!ic || !layout || layout->mask_stride <= 0) {
		return 0;
	}
	image_count = ST16_Icon_Image_Count(base, blob_size, ic);
	if (image_count == 0) {
		return 0;
	}
	return image_count * (size_t)layout->mask_stride;
}

static BOOL ST16_Offset_In_Bounds(uint32_t off, size_t need, size_t blob_size)
{
	if (off == 0) {
		return TRUE;
	}
	return off <= blob_size && need <= blob_size && (size_t)off + need <= blob_size;
}

static BOOL ST16_Validate_Tail(const uint8_t *base, size_t blob_size, const ST16_IControlView *ic, size_t data_end)
{
	if (ic->transflag_off != 0) {
		if (ic->transflag_off < data_end) {
			return FALSE;
		}
		{
			size_t const trans_bytes =
				ST16_Transflag_Byte_Count(base, blob_size, ic);

			if (trans_bytes == 0
				|| !ST16_Offset_In_Bounds(
					ic->transflag_off,
					trans_bytes,
					blob_size)) {
				return FALSE;
			}
		}
	}
	if (ic->map_off != 0) {
		if (ic->map_off < data_end) {
			return FALSE;
		}
		if (ic->map_off >= blob_size) {
			return FALSE;
		}
	}
	if (ic->size != 0 && ic->size > blob_size) {
		return FALSE;
	}
	return TRUE;
}

BOOL ST16_Validate(const uint8_t *base, size_t blob_size)
{
	ST16_IControlView ic;
	size_t data_end;
	uint16_t flags = 0;

	if (!ST16_Parse_IControl(base, blob_size, &ic)) {
		return FALSE;
	}

	if (ST16_Is_Standard(base, blob_size)) {
		size_t icon_bytes = (size_t)ic.width * (size_t)ic.height;
		size_t image_count;
		if (icon_bytes == 0) {
			return FALSE;
		}
		image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
		if (image_count == 0) {
			return FALSE;
		}
		data_end = (size_t)ic.icons_off + image_count * icon_bytes;
		if (!ST16_Offset_In_Bounds(ic.icons_off, image_count * icon_bytes, blob_size)) {
			return FALSE;
		}
		return ST16_Validate_Tail(base, blob_size, &ic, data_end);
	}

	if (!ST16_Read_Chunk_Flags(base, blob_size, &flags)) {
		return FALSE;
	}
	if (ic.icons_off < ST16_ICONS_V1) {
		return FALSE;
	}

	{
		ST16_PlanarLayout layout;
		BOOL const has_mask = (flags & ST16_FLAG_HAS_MASK) ? TRUE : FALSE;
		size_t image_count;
		size_t icons_bytes;

		ST16_Compute_Planar_Layout((int)ic.width, (int)ic.height, has_mask, &layout);
		if (layout.planar_stride <= 0 || layout.icon_stride <= 0) {
			return FALSE;
		}
		image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
		if (image_count == 0) {
			return FALSE;
		}
		icons_bytes = image_count * (size_t)layout.icon_stride;
		if (!ST16_Offset_In_Bounds(ic.icons_off, icons_bytes, blob_size)) {
			return FALSE;
		}
		data_end = (size_t)ic.icons_off + icons_bytes;
		return ST16_Validate_Tail(base, blob_size, &ic, data_end);
	}
}

void ST16_Native_Swap_Header(IControl_Type *ic)
{
	uint8_t *const base = (uint8_t *)ic;

	if (!ic) {
		return;
	}

	ST16_Write_BE16(base + 0, ST16_Read_LE16(base + 0));
	ST16_Write_BE16(base + 2, ST16_Read_LE16(base + 2));
	ST16_Write_BE16(base + 4, ST16_Read_LE16(base + 4));
	ST16_Write_BE16(base + 6, ST16_Read_LE16(base + 6));
	ST16_Write_BE32(base + 8, ST16_Read_LE32(base + 8));
	ST16_Write_BE32(base + 12, ST16_Read_LE32(base + 12));
	ST16_Write_BE32(base + 16, ST16_Read_LE32(base + 16));
	ST16_Write_BE32(base + 20, ST16_Read_LE32(base + 20));
	ST16_Write_BE32(base + 24, ST16_Read_LE32(base + 24));
	ST16_Write_BE32(base + 28, ST16_Read_LE32(base + 28));

	ST16_Write_BE32(base + ST16_CHUNK_OFFSET + 4, ST16_Read_LE32(base + ST16_CHUNK_OFFSET + 4));
	ST16_Write_BE16(base + ST16_CHUNK_OFFSET + 8, ST16_Read_LE16(base + ST16_CHUNK_OFFSET + 8));
	ST16_Write_BE16(base + ST16_CHUNK_OFFSET + 10, ST16_Read_LE16(base + ST16_CHUNK_OFFSET + 10));
}

BOOL ST16_Validate_Native(const IControl_Type *ic, size_t blob_size)
{
	ST16_IControlView view;
	const uint8_t *base = (const uint8_t *)ic;
	size_t data_end;
	const ST16_Chunk_Type *chunk;
	uint16_t flags;

	if (!ic || blob_size < ST16_ICONTROL_SIZE) {
		return FALSE;
	}
	if (!ST16_Has_Native_Chunk(ic)) {
		return FALSE;
	}

	ST16_IControlView_From_Struct(ic, &view);
	if (view.width <= 0 || view.height <= 0 || view.width > 128 || view.height > 128) {
		return FALSE;
	}
	if (view.count <= 0) {
		return FALSE;
	}
	if (view.icons_off < ST16_ICONS_V1) {
		return FALSE;
	}

	chunk = ST16_Chunk(ic);
	if (chunk->size != ST16_PAYLOAD_SIZE || chunk->reserved != 0) {
		return FALSE;
	}
	flags = chunk->flags;

	{
		ST16_PlanarLayout layout;
		BOOL const has_mask = (flags & ST16_FLAG_HAS_MASK) ? TRUE : FALSE;
		size_t image_count;
		size_t icons_bytes;

		ST16_Compute_Planar_Layout((int)view.width, (int)view.height, has_mask, &layout);
		if (layout.planar_stride <= 0 || layout.icon_stride <= 0) {
			return FALSE;
		}
		image_count = ST16_Icon_Image_Count(base, blob_size, &view);
		if (image_count == 0) {
			return FALSE;
		}
		icons_bytes = image_count * (size_t)layout.icon_stride;
		if (!ST16_Offset_In_Bounds(view.icons_off, icons_bytes, blob_size)) {
			return FALSE;
		}
		data_end = (size_t)view.icons_off + icons_bytes;
		return ST16_Validate_Tail(base, blob_size, &view, data_end);
	}
}

BOOL ST16_Resolve_Icon_Index(
	const uint8_t *base,
	size_t blob_size,
	int logical_icon,
	int *image_index_out)
{
	ST16_IControlView ic;
	int image_index;
	size_t image_count;

	if (!base || !image_index_out || logical_icon < 0) {
		return FALSE;
	}

	if (ST16_Has_Native_Chunk((const IControl_Type *)base)) {
		ST16_IControlView_From_Struct((const IControl_Type *)base, &ic);
	} else if (!ST16_Parse_IControl(base, blob_size, &ic)) {
		return FALSE;
	}

	image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
	if (image_count == 0) {
		return FALSE;
	}

	if (ic.map_off > 0) {
		if (ic.count == 0 || (size_t)logical_icon >= (size_t)ic.count) {
			return FALSE;
		}
		if (ic.map_off + (uint32_t)logical_icon >= (uint32_t)blob_size) {
			return FALSE;
		}
		image_index = (int)base[ic.map_off + (size_t)logical_icon];
	} else {
		if (logical_icon >= (int)image_count) {
			return FALSE;
		}
		image_index = logical_icon;
	}

	if (image_index < 0 || (size_t)image_index >= image_count) {
		return FALSE;
	}
	if ((uint8_t)image_index == 0xFF) {
		return FALSE;
	}
	*image_index_out = image_index;
	return TRUE;
}

const uint8_t *ST16_Standard_Icon_Ptr(const uint8_t *base, size_t blob_size, int image_index)
{
	ST16_IControlView ic;
	size_t icon_bytes;
	size_t off;
	size_t image_count;

	if (!ST16_Is_Standard(base, blob_size)) {
		return NULL;
	}
	if (!ST16_Parse_IControl(base, blob_size, &ic)) {
		return NULL;
	}
	image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
	if (image_index < 0 || (size_t)image_index >= image_count) {
		return NULL;
	}

	icon_bytes = (size_t)ic.width * (size_t)ic.height;
	off = (size_t)ic.icons_off + (size_t)image_index * icon_bytes;
	if (!ST16_Offset_In_Bounds((uint32_t)off, icon_bytes, blob_size)) {
		return NULL;
	}
	return base + off;
}

const uint8_t *ST16_Planar_Icon_Ptr(
	const uint8_t *base,
	size_t blob_size,
	int image_index,
	ST16_PlanarLayout *layout_out)
{
	ST16_IControlView ic;
	ST16_PlanarLayout layout;
	uint16_t flags = 0;
	size_t off;
	size_t image_count;

	if (!layout_out || !base || blob_size < ST16_ICONTROL_SIZE) {
		return NULL;
	}

	if (!ST16_Has_Native_Chunk((const IControl_Type *)base)) {
		return NULL;
	}
	ST16_IControlView_From_Struct((const IControl_Type *)base, &ic);
	{
		const ST16_Chunk_Type *chunk = ST16_Chunk((const IControl_Type *)base);

		if (chunk->size != ST16_PAYLOAD_SIZE || chunk->reserved != 0) {
			return NULL;
		}
		flags = chunk->flags;
	}
	image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
	if (image_index < 0 || (size_t)image_index >= image_count) {
		return NULL;
	}

	ST16_Compute_Planar_Layout(
		(int)ic.width,
		(int)ic.height,
		(flags & ST16_FLAG_HAS_MASK) ? TRUE : FALSE,
		&layout);
	*layout_out = layout;
	if (layout.planar_stride <= 0) {
		return NULL;
	}

	off = (size_t)ic.icons_off + (size_t)image_index * (size_t)layout.icon_stride;
	if (!ST16_Offset_In_Bounds((uint32_t)off, (size_t)layout.planar_stride, blob_size)) {
		return NULL;
	}
	return base + off;
}

const uint8_t *ST16_Mask_Icon_Ptr(
	const uint8_t *base,
	size_t blob_size,
	int image_index,
	const ST16_PlanarLayout *layout)
{
	ST16_PlanarLayout planar_layout;
	const uint8_t *planar;
	size_t off;

	(void)layout;
	if (!base || blob_size < ST16_ICONTROL_SIZE
		|| !ST16_Has_Native_Chunk((const IControl_Type *)base)) {
		return NULL;
	}

	planar = ST16_Planar_Icon_Ptr(base, blob_size, image_index, &planar_layout);
	if (!planar || planar_layout.mask_stride <= 0 || planar_layout.planar_stride <= 0) {
		return NULL;
	}

	off = (size_t)(planar - base) + (size_t)planar_layout.planar_stride;
	if (!ST16_Offset_In_Bounds((uint32_t)off, (size_t)planar_layout.mask_stride, blob_size)) {
		return NULL;
	}
	return base + off;
}

void ST16_Write_Chunk(uint8_t *base, uint16_t flags)
{
	if (!base) {
		return;
	}
	memcpy(base + ST16_CHUNK_OFFSET, "ST16", 4);
	ST16_Write_LE32(base + ST16_CHUNK_OFFSET + 4, ST16_PAYLOAD_SIZE);
	ST16_Write_LE16(base + ST16_CHUNK_OFFSET + 8, flags);
	ST16_Write_LE16(base + ST16_CHUNK_OFFSET + 10, 0);
}
