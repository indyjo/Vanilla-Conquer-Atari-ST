/*
 * st16_iconset.h - ST16 extended ICN/.TEM iconset format (Atari ST port)
 *
 * Standard Westwood iconsets use chunky 8bpp pixels at Icons=0x20.
 * ST16 adds a 12-byte chunk at file offset 0x20 and blitter-ready planar
 * icon data at Icons=0x2c. See tiberiandawn/atari.md.
 */

#ifndef ST16_ICONSET_H
#define ST16_ICONSET_H

#include "function.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IControl_Type (TD) is 32 bytes; must match tile.h */
#define ST16_ICONTROL_SIZE       32u

/* Icons field values (offset +12 from blob start) */
#define ST16_ICONS_STANDARD      0x20u
#define ST16_ICONS_RESERVED      0x28u /* not used for TD Icons; avoids RA offset confusion */
#define ST16_ICONS_V1            0x2cu

#define ST16_CHUNK_OFFSET        0x20u
#define ST16_CHUNK_TOTAL         12u
#define ST16_PAYLOAD_SIZE        4u

/* 'ST16' little-endian */
#define ST16_MAGIC               0x36315453u

#define ST16_FLAG_HAS_MASK       0x0001u

/*
 * Canonical planar layout for 24x24 terrain tiles (planar_w=32, 16-color interleaved).
 * Matches ICON_PIXEL_W/H in display.h; kept here so draw/convert avoid per-stamp math.
 */
#define ST16_TILE_W                  24
#define ST16_TILE_H                  24
#define ST16_TILE_PLANAR_W           32
#define ST16_TILE_PLANAR_ROW_BYTES   16
#define ST16_TILE_PLANAR_STRIDE      384
#define ST16_TILE_MASK_ROW_BYTES     4
#define ST16_TILE_MASK_STRIDE        96
#define ST16_TILE_ICON_STRIDE        384
#define ST16_TILE_ICON_STRIDE_MASK   480

/* Max Width*Height from ST16_Parse_IControl (128×128). */
#define ST16_CHUNKY_ICON_MAX_BYTES  (128u * 128u)

#pragma pack(push, 1)
typedef struct ST16_Chunk_Type
{
	uint32_t magic;
	uint32_t size;
	uint16_t flags;
	uint16_t reserved;
} ST16_Chunk_Type;
#pragma pack(pop)

typedef struct ST16_IControlView
{
	uint16_t width;
	uint16_t height;
	uint16_t count;
	uint32_t size;
	uint32_t icons_off;
	uint32_t transflag_off;
	uint32_t map_off;
} ST16_IControlView;

typedef struct ST16_PlanarLayout
{
	int planar_w;
	int planar_h;
	int planar_row_bytes;
	int planar_stride;
	int mask_row_bytes;
	int mask_stride;
	/* planar_stride, or planar_stride + mask_stride when HAS_MASK (per-icon packing). */
	int icon_stride;
} ST16_PlanarLayout;

uint16_t ST16_Read_LE16(const uint8_t *p);
uint32_t ST16_Read_LE32(const uint8_t *p);
void ST16_Write_LE16(uint8_t *p, uint16_t v);
void ST16_Write_LE32(uint8_t *p, uint32_t v);

/*
 * Parse the fixed IControl header. Does not validate tail layout.
 * Returns FALSE if base is NULL, blob too small, or width/height/count invalid.
 */
BOOL ST16_Parse_IControl(const uint8_t *base, size_t blob_size, ST16_IControlView *out);

/*
 * TRUE when the iconset uses per-image transparency (TransFlag byte non-zero).
 * Converted ST16 blobs store a 1bpp mask per icon when any TransFlag is set (HAS_MASK).
 */
BOOL ST16_Iconset_Uses_Mask(const uint8_t *base, size_t blob_size);

/* TRUE when Icons=0x20 (standard chunky 8bpp ICN). */
BOOL ST16_Is_Standard(const uint8_t *base, size_t blob_size);

/* TRUE when Icons>=0x2c and the ST16 chunk at 0x20 is present and valid. */
BOOL ST16_Is_Native(const uint8_t *base, size_t blob_size);

/*
 * Read ST16 chunk flags (offset 0x28). reserved must be 0.
 * Returns FALSE if not native or chunk malformed.
 */
BOOL ST16_Read_Chunk_Flags(const uint8_t *base, size_t blob_size, uint16_t *flags_out);

/* ST16 chunk at offset 0x20; flags_out may be NULL. */
BOOL ST16_Chunk_Valid(const uint8_t *base, size_t blob_size, uint16_t *flags_out);

/*
 * Planar/mask byte layout for one icon image.
 * Native blobs pack each image as [planar][mask] when HAS_MASK; icon_stride is the
 * byte distance to the next image. Width is rounded up to a 16-pixel boundary.
 */
static inline void ST16_Fill_Planar_Layout_24x24(BOOL has_mask, ST16_PlanarLayout *out)
{
	if (!out) {
		return;
	}

	out->planar_w = ST16_TILE_PLANAR_W;
	out->planar_h = ST16_TILE_H;
	out->planar_row_bytes = ST16_TILE_PLANAR_ROW_BYTES;
	out->planar_stride = ST16_TILE_PLANAR_STRIDE;
	out->icon_stride = ST16_TILE_ICON_STRIDE;
	if (has_mask) {
		out->mask_row_bytes = ST16_TILE_MASK_ROW_BYTES;
		out->mask_stride = ST16_TILE_MASK_STRIDE;
		out->icon_stride = ST16_TILE_ICON_STRIDE_MASK;
	} else {
		out->mask_row_bytes = 0;
		out->mask_stride = 0;
	}
}

void ST16_Compute_Planar_Layout(int width, int height, BOOL has_mask, ST16_PlanarLayout *out);

/*
 * Zero planar columns [tile_w .. planar_w) for rows [0 .. tile_h).
 * C2P only writes the logical tile width; leftover bits in the 16-aligned slab
 * would otherwise retain pre-conversion chunky bytes.
 */
void ST16_Clear_Planar_Icon_Padding(
	uint8_t *planar_icon,
	const ST16_PlanarLayout *layout,
	int tile_w,
	int tile_h);

/*
 * Build 1bpp mask (ST blitter layout) from chunky 8bpp icon bytes.
 * per_pixel_trans: when TRUE, palette index 0 preserves backdrop; otherwise all pixels draw.
 */
void ST16_Build_Mask_From_Chunky(
	const uint8_t *chunky,
	int width,
	int height,
	BOOL per_pixel_trans,
	uint8_t *mask_out,
	const ST16_PlanarLayout *layout);

/* Total bytes occupied by all mask planes when HAS_MASK (else 0). */
size_t ST16_Mask_Icons_Bytes(
	const uint8_t *base,
	size_t blob_size,
	const ST16_IControlView *ic,
	const ST16_PlanarLayout *layout);
size_t ST16_Planar_Icons_Bytes(
	const uint8_t *base,
	size_t blob_size,
	const ST16_IControlView *ic,
	const ST16_PlanarLayout *layout);

/*
 * Number of chunky/planar image blobs before the Map table.
 * IControl Count is the logical map size (template cells); it can exceed this.
 */
size_t ST16_Icon_Image_Count(const uint8_t *base, size_t blob_size, const ST16_IControlView *ic);

/*
 * Bytes in the TransFlag table. Westwood often stores one byte per physical image
 * (icons|map|transflag), which can be fewer than IControl Count (map slots).
 */
size_t ST16_Transflag_Byte_Count(
	const uint8_t *base,
	size_t blob_size,
	const ST16_IControlView *ic);

/*
 * Full blob validation for standard or native iconsets.
 * Checks offsets, icon region, and optional TransFlag/Map tables.
 */
BOOL ST16_Validate(const uint8_t *base, size_t blob_size);

/*
 * Map logical stamp index (template cell slot) to image index in the Icons array.
 * Without Map, logical_icon must be < image count.
 */
BOOL ST16_Resolve_Icon_Index(
	const uint8_t *base,
	size_t blob_size,
	int logical_icon,
	int *image_index_out);

const uint8_t *ST16_Standard_Icon_Ptr(
	const uint8_t *base,
	size_t blob_size,
	int image_index);

const uint8_t *ST16_Planar_Icon_Ptr(
	const uint8_t *base,
	size_t blob_size,
	int image_index,
	ST16_PlanarLayout *layout_out);

const uint8_t *ST16_Mask_Icon_Ptr(
	const uint8_t *base,
	size_t blob_size,
	int image_index,
	const ST16_PlanarLayout *layout);

/* Write ST16 chunk at offset 0x20 (12 bytes). reserved is forced to 0. */
void ST16_Write_Chunk(uint8_t *base, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif /* ST16_ICONSET_H */
