/*
 * st16_convert.cpp - In-place standard ICN -> ST16 conversion
 */

#include "st16_convert.h"
#include "st16_iconset.h"
#include "c2p.h"
#include "memflag.h"

#include <stdio.h>
#include <string.h>

/* Per-icon chunky scratch for the only runtime convert we keep (TRANS / 24×24). */
#define ST16_RUNTIME_CHUNKY_SCRATCH_BYTES ((size_t)ST16_TILE_W * (size_t)ST16_TILE_H)

static uint8_t *g_trans_icn_original = NULL;
static size_t g_trans_icn_original_size = 0;

static BOOL ST16_Convert_With_Stack_Scratch(uint8_t *base, size_t blob_size)
{
	uint8_t scratch[ST16_RUNTIME_CHUNKY_SCRATCH_BYTES];
	ST16_IControlView ic;

	if (!ST16_Parse_IControl(base, blob_size, &ic)) {
		return FALSE;
	}
	if ((size_t)ic.width * (size_t)ic.height > ST16_RUNTIME_CHUNKY_SCRATCH_BYTES) {
		return FALSE;
	}
	return ST16_Convert_InPlace(base, blob_size, scratch);
}

BOOL ST16_Trans_Iconset_Capture(const void *icondata)
{
	const uint8_t *base;
	size_t size;

	if (!icondata) {
		return FALSE;
	}
	if (g_trans_icn_original) {
		return TRUE;
	}

	base = (const uint8_t *)icondata;
	if (ST16_Has_Native_Chunk((const IControl_Type *)icondata)) {
		printf("ST16: TRANS.ICN already ST16; cannot capture original\n");
		return FALSE;
	}

	size = (size_t)ST16_Read_LE32(base + 8);
	if (size < ST16_ICONTROL_SIZE || size > 64u * 1024u) {
		return FALSE;
	}
	if (!ST16_Is_Standard(base, size) || !ST16_Iconset_Should_Convert(base, size)) {
		printf("ST16: TRANS.ICN not convertible; capture skipped\n");
		return FALSE;
	}

	g_trans_icn_original = (uint8_t *)Alloc((unsigned long)size, MEM_NORMAL);
	if (!g_trans_icn_original) {
		printf("ST16: failed to allocate TRANS.ICN original (%lu bytes)\n", (unsigned long)size);
		return FALSE;
	}
	memcpy(g_trans_icn_original, base, size);
	g_trans_icn_original_size = size;
	return TRUE;
}

void ST16_Trans_Iconset_Restore_And_Convert(void *icondata)
{
	if (!icondata || !g_trans_icn_original || g_trans_icn_original_size == 0) {
		return;
	}
	if (!C2P_Weights_Are_Ready()) {
		return;
	}

	memcpy(icondata, g_trans_icn_original, g_trans_icn_original_size);
	if (!ST16_Convert_With_Stack_Scratch((uint8_t *)icondata, g_trans_icn_original_size)) {
		printf("ST16: TRANS.ICN restore/convert failed\n");
	}
}

void ST16_Prewarm_Iconset(const void *icondata)
{
	const uint8_t *base;
	size_t blob_size;

	if (!icondata || !C2P_Weights_Are_Ready()) {
		return;
	}
	if (ST16_Has_Native_Chunk((const IControl_Type *)icondata)) {
		return;
	}

	base = (const uint8_t *)icondata;
	blob_size = (size_t)ST16_Read_LE32(base + 8);
	if (!ST16_Iconset_Should_Convert(base, blob_size)) {
		return;
	}
	(void)ST16_Convert_With_Stack_Scratch((uint8_t *)icondata, blob_size);
}

static uint32_t ST16_Adjust_Tail_Offset(uint32_t off, size_t tail_old, size_t shrink)
{
	if (off == 0) {
		return 0;
	}
	if ((size_t)off >= tail_old) {
		return off - (uint32_t)shrink;
	}
	return off;
}

BOOL ST16_Iconset_Should_Convert(const uint8_t *base, size_t blob_size)
{
	ST16_IControlView ic;
	size_t icon_bytes;
	size_t image_count;
	size_t data_end;

	if (!base || blob_size < ST16_ICONTROL_SIZE) {
		return FALSE;
	}
	if (!ST16_Is_Standard(base, blob_size)) {
		return FALSE;
	}
	if (!ST16_Parse_IControl(base, blob_size, &ic)) {
		return FALSE;
	}

	icon_bytes = (size_t)ic.width * (size_t)ic.height;
	if (icon_bytes == 0 || icon_bytes > ST16_CHUNKY_ICON_MAX_BYTES) {
		return FALSE;
	}
	image_count = ST16_Icon_Image_Count(base, blob_size, &ic);
	if (image_count == 0) {
		return FALSE;
	}
	data_end = (size_t)ic.icons_off + image_count * icon_bytes;
	if (data_end > blob_size) {
		return FALSE;
	}
	if (ic.transflag_off != 0) {
		size_t const trans_bytes = ST16_Transflag_Byte_Count(base, blob_size, &ic);

		if (ic.transflag_off < data_end || trans_bytes == 0
			|| (size_t)ic.transflag_off + trans_bytes > blob_size) {
			return FALSE;
		}
	}
	if (ic.map_off != 0) {
		if (ic.map_off < data_end || ic.map_off >= blob_size) {
			return FALSE;
		}
	}
	return TRUE;
}

BOOL ST16_Convert_InPlace(uint8_t *base, size_t size, uint8_t *scratch_chunky)
{
	ST16_IControlView ic;
	ST16_PlanarLayout layout;
	BOOL has_mask;
	size_t chunky_stride;
	size_t icons_old;
	size_t icons_new;
	size_t image_count;
	size_t chunky_bytes;
	size_t icons_bytes;
	size_t tail_old;
	size_t tail_new;
	size_t shrink;
	size_t tail_len;
	size_t new_size;
	uint16_t chunk_flags;
	int i;

	if (!base || !scratch_chunky || size < ST16_ICONTROL_SIZE) {
		return FALSE;
	}
	/* Already fully converted (native magic + native header fields). */
	if (ST16_Has_Native_Chunk((const IControl_Type *)base)) {
		return TRUE;
	}
	if (!ST16_Iconset_Should_Convert(base, size)) {
		return FALSE;
	}
	if (!ST16_Parse_IControl(base, size, &ic)) {
		return FALSE;
	}

	has_mask = ST16_Iconset_Uses_Mask(base, size);
	ST16_Compute_Planar_Layout((int)ic.width, (int)ic.height, has_mask, &layout);
	if (layout.planar_stride <= 0) {
		return FALSE;
	}
	if (has_mask && layout.mask_stride <= 0) {
		return FALSE;
	}

	chunky_stride = (size_t)ic.width * (size_t)ic.height;
	icons_old = (size_t)ic.icons_off;
	icons_new = (size_t)ST16_ICONS_V1;
	image_count = ST16_Icon_Image_Count(base, size, &ic);
	if (image_count == 0) {
		return FALSE;
	}
	chunky_bytes = image_count * chunky_stride;
	icons_bytes = image_count * (size_t)layout.icon_stride;
	tail_old = icons_old + chunky_bytes;
	tail_new = icons_new + icons_bytes;

	if (tail_old > size || tail_new > tail_old) {
		return FALSE;
	}
	shrink = tail_old - tail_new;
	new_size = size - shrink;
	tail_len = size - tail_old;
	chunk_flags = has_mask ? ST16_FLAG_HAS_MASK : 0;

	/*
	 * Per-icon [planar|mask] packing: each image's mask follows its planar slab.
	 * Snapshot chunky sources before writes when masked (in-place overlap safety).
	 */
	{
		uint8_t *chunky_snapshot = NULL;
		uint8_t *transflag_snapshot = NULL;
		size_t transflag_bytes = 0;
		const uint8_t *chunky_src_base = base + icons_old;

		if (has_mask) {
			/*
			 * Temporary snapshot only when converting masked standard ICN.
			 * Remixed ST16 packs hit the native fast path and never reach here;
			 * do not pre-reserve this (it cost 128 KiB ST-RAM and OOMed theater Cache).
			 */
			chunky_snapshot = (uint8_t *)Alloc((unsigned long)chunky_bytes, MEM_NORMAL);
			if (!chunky_snapshot) {
				printf("ST16: convert failed: chunky snapshot alloc (%lu bytes)\n",
					(unsigned long)chunky_bytes);
				return FALSE;
			}
			memcpy(chunky_snapshot, base + icons_old, chunky_bytes);
			chunky_src_base = chunky_snapshot;

			if (ic.transflag_off != 0) {
				transflag_bytes = ST16_Transflag_Byte_Count(base, size, &ic);
				if (transflag_bytes > 0) {
					transflag_snapshot =
						(uint8_t *)Alloc((unsigned long)transflag_bytes, MEM_NORMAL);
					if (!transflag_snapshot) {
						printf(
							"ST16: convert failed: transflag snapshot alloc (%lu bytes)\n",
							(unsigned long)transflag_bytes);
						Free(chunky_snapshot);
						return FALSE;
					}
					memcpy(
						transflag_snapshot,
						base + (size_t)ic.transflag_off,
						transflag_bytes);
				}
			}
		}

		for (i = 0; i < (int)image_count; ++i) {
			size_t const src_off = (size_t)i * chunky_stride;
			const uint8_t *const src_ptr = chunky_src_base + src_off;
			uint8_t *const dst_planar =
				base + icons_new + (size_t)i * (size_t)layout.icon_stride;
			BOOL per_pixel_trans = FALSE;
			size_t const dst_slot_bytes = has_mask
				? (size_t)layout.icon_stride
				: (size_t)layout.planar_stride;

			if (src_off + chunky_stride > chunky_bytes
				|| dst_planar + dst_slot_bytes > base + tail_new) {
				printf("ST16: convert failed at icon %d: source bounds\n", i);
				if (transflag_snapshot) {
					Free(transflag_snapshot);
				}
				if (chunky_snapshot) {
					Free(chunky_snapshot);
				}
				return FALSE;
			}

			if (has_mask && transflag_snapshot && (size_t)i < transflag_bytes) {
				per_pixel_trans = transflag_snapshot[i] != 0;
			}

			memcpy(scratch_chunky, src_ptr, chunky_stride);

			memset(dst_planar, 0, dst_slot_bytes);
			C2P_Render_Logical_To_Planar_Rect(
				scratch_chunky,
				(int)ic.width,
				(int)ic.height,
				(int)ic.width,
				dst_planar,
				layout.planar_row_bytes,
				layout.planar_w,
				layout.planar_h,
				0,
				0,
				0,
				0);
			ST16_Clear_Planar_Icon_Padding(
				dst_planar,
				&layout,
				(int)ic.width,
				(int)ic.height);

			if (has_mask) {
				uint8_t *const dst_mask = dst_planar + (size_t)layout.planar_stride;

				if (dst_mask + (size_t)layout.mask_stride > base + tail_new) {
					printf("ST16: convert failed at icon %d: mask bounds\n", i);
					if (transflag_snapshot) {
						Free(transflag_snapshot);
					}
					if (chunky_snapshot) {
						Free(chunky_snapshot);
					}
					return FALSE;
				}
				ST16_Build_Mask_From_Chunky(
					scratch_chunky,
					(int)ic.width,
					(int)ic.height,
					per_pixel_trans,
					dst_mask,
					&layout);
			}
		}

		if (transflag_snapshot) {
			Free(transflag_snapshot);
		}
		if (chunky_snapshot) {
			Free(chunky_snapshot);
		}
	}

	/* Move Map / TransFlag only after all chunky sources are read; tail_new can
	 * lie inside the old chunky region (e.g. CLEAR1.TEM image 10). */
	if (tail_len > 0) {
		memmove(base + tail_new, base + tail_old, tail_len);
	}

	ST16_Write_Chunk(base, chunk_flags);
	ST16_Write_LE32(base + 12, ST16_ICONS_V1);
	ST16_Write_LE32(base + 8, (uint32_t)new_size);
	ST16_Write_LE32(
		base + 24,
		ST16_Adjust_Tail_Offset(ic.transflag_off, tail_old, shrink));
	ST16_Write_LE32(base + 28, ST16_Adjust_Tail_Offset(ic.map_off, tail_old, shrink));

	/* Swap all numeric IControl_Type fields and ST16 chunk numerics to native
	 * (big-endian) order. Chunk magic bytes are NOT swapped — they read as
	 * ST16_MAGIC_NATIVE on a 68000 native load both before and after. */
	ST16_Native_Swap_Header((IControl_Type *)base);
	return TRUE;
}

const void *ST16_Iconset_Resolve(const void *icondata, uint8_t *scratch_chunky)
{
	const uint8_t *base;
	size_t blob_size;
	uint8_t *writable;

	if (!icondata) {
		return NULL;
	}

	/* Already native — fast exit. */
	if (ST16_Has_Native_Chunk((const IControl_Type *)icondata)) {
		return icondata;
	}

	base = (const uint8_t *)icondata;
	blob_size = (size_t)ST16_Read_LE32(base + 8);

	if (blob_size < ST16_ICONTROL_SIZE) {
		return NULL;
	}

	if (!ST16_Iconset_Should_Convert(base, blob_size)) {
		return NULL;
	}
	if (!C2P_Weights_Are_Ready()) {
		return NULL;
	}

	writable = (uint8_t *)icondata;
	if (scratch_chunky) {
		if (!ST16_Convert_InPlace(writable, blob_size, scratch_chunky)) {
			return NULL;
		}
	} else if (!ST16_Convert_With_Stack_Scratch(writable, blob_size)) {
		return NULL;
	}

	return icondata;
}
