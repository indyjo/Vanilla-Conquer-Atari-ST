/*
 * Automated: ST16 in-place ICN conversion (multi-icon planar slabs).
 * No display hardware required beyond C2P weight install.
 */

#include "st16_convert_autotest.h"

#include "c2p.h"
#include "palette.h"
#include "st16_convert.h"
#include "st16_iconset.h"
#include "st_temperat_palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ST_TILE_W = 24,
	ST_TILE_H = 24,
	ST_TILE_CHUNKY = ST_TILE_W * ST_TILE_H,
	ST_ICON_COUNT = 2
};

static size_t st16_build_standard_blob(uint8_t *out, size_t cap, const uint8_t *chunky_icons)
{
	const size_t icons_bytes = (size_t)ST_ICON_COUNT * (size_t)ST_TILE_CHUNKY;
	const size_t total = (size_t)ST16_ICONTROL_SIZE + icons_bytes;

	if (!out || cap < total || !chunky_icons) {
		return 0;
	}

	memset(out, 0, total);
	ST16_Write_LE16(out + 0, ST_TILE_W);
	ST16_Write_LE16(out + 2, ST_TILE_H);
	ST16_Write_LE16(out + 4, ST_ICON_COUNT);
	ST16_Write_LE32(out + 8, (uint32_t)total);
	ST16_Write_LE32(out + 12, ST16_ICONS_STANDARD);
	memcpy(out + ST16_ICONTROL_SIZE, chunky_icons, icons_bytes);
	return total;
}

static void st16_fill_synthetic_chunky(uint8_t *chunky_icons, int seed)
{
	int i;

	for (i = 0; i < ST_ICON_COUNT * ST_TILE_CHUNKY; ++i) {
		const int x = i % ST_TILE_W;
		const int y = (i / ST_TILE_W) % ST_TILE_H;
		const int icon = i / ST_TILE_CHUNKY;
		chunky_icons[i] = (uint8_t)(1 + (((x * 7 + y * 5 + seed + icon * 13) ^ (x + y)) & 15));
	}
}

static void st16_bake_reference_icon(
	uint8_t *dst_planar,
	const uint8_t *chunky_icon,
	int width,
	int height,
	const ST16_PlanarLayout *layout)
{
	memset(dst_planar, 0, (size_t)layout->planar_stride);
	C2P_Render_Logical_To_Planar_Rect(
		chunky_icon,
		width,
		height,
		width,
		dst_planar,
		layout->planar_row_bytes,
		layout->planar_w,
		layout->planar_h,
		0,
		0,
		0,
		0);
	ST16_Clear_Planar_Icon_Padding(dst_planar, layout, width, height);
}

static int st16_report_slab_diff(
	const uint8_t *got,
	const uint8_t *ref,
	size_t nbytes,
	int icon_index)
{
	size_t off;

	for (off = 0; off < nbytes; ++off) {
		if (got[off] != ref[off]) {
			printf(
				"  FAIL icon %d slab byte %lu (got=0x%02x ref=0x%02x)\n",
				icon_index,
				(unsigned long)off,
				(unsigned)got[off],
				(unsigned)ref[off]);
			return 1;
		}
	}
	return 0;
}

enum {
	ST_WIDE_W = 10,
	ST_WIDE_H = 16,
	ST_WIDE_CHUNKY = ST_WIDE_W * ST_WIDE_H,
	ST_WIDE_ICON_COUNT = 1,
	ST_CLEAR1_ICON_COUNT = 16,
	ST_CLEAR1_MAP_BYTES = 32
};

static int st16_run_clear1_tail_overlap_check(void)
{
	ST16_PlanarLayout layout;
	uint8_t *blob = NULL;
	uint8_t scratch[ST_TILE_CHUNKY];
	uint8_t ref_planar[384];
	size_t const icons_bytes = (size_t)ST_CLEAR1_ICON_COUNT * (size_t)ST_TILE_CHUNKY;
	size_t const tail_old = (size_t)ST16_ICONTROL_SIZE + icons_bytes;
	size_t const blob_size = tail_old + (size_t)ST_CLEAR1_MAP_BYTES;
	size_t new_size;
	int fails = 0;
	int i;

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, FALSE, &layout);
	if (layout.planar_stride <= 0) {
		printf("  ST16 convert CLEAR1 tail: FAIL (layout)\n");
		return 1;
	}

	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 convert CLEAR1 tail: FAIL (alloc)\n");
		return 1;
	}
	memset(blob, 0, blob_size);
	ST16_Write_LE16(blob + 0, ST_TILE_W);
	ST16_Write_LE16(blob + 2, ST_TILE_H);
	ST16_Write_LE16(blob + 4, ST_CLEAR1_ICON_COUNT);
	ST16_Write_LE32(blob + 8, (uint32_t)blob_size);
	ST16_Write_LE32(blob + 12, ST16_ICONS_STANDARD);
	ST16_Write_LE32(blob + 28, (uint32_t)tail_old);
	for (i = 0; i < (int)icons_bytes; ++i) {
		blob[ST16_ICONTROL_SIZE + i] = (uint8_t)(1 + (i & 15));
	}
	for (i = 0; i < ST_CLEAR1_MAP_BYTES; ++i) {
		blob[tail_old + (size_t)i] = (uint8_t)(0xA0 + i);
	}

	st16_bake_reference_icon(
		ref_planar,
		blob + ST16_ICONTROL_SIZE + (size_t)10 * (size_t)ST_TILE_CHUNKY,
		ST_TILE_W,
		ST_TILE_H,
		&layout);

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		printf("  FAIL CLEAR1 tail: Convert_InPlace\n");
		free(blob);
		return 1;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	{
		const uint8_t *got = ST16_Planar_Icon_Ptr(blob, new_size, 10, &layout);
		if (!got || st16_report_slab_diff(got, ref_planar, (size_t)layout.planar_stride, 10)) {
			fails++;
		}
	}

	free(blob);
	if (fails == 0) {
		printf("  ST16 convert CLEAR1 tail: PASS (icon 10)\n");
	}
	return fails;
}

static int st16_run_wide_convert_check(void)
{
	ST16_PlanarLayout layout;
	uint8_t chunky[ST_WIDE_CHUNKY];
	uint8_t ref_planar[256];
	uint8_t scratch[ST_WIDE_CHUNKY];
	uint8_t *blob = NULL;
	size_t blob_size;
	size_t new_size;
	int fails = 0;

	ST16_Compute_Planar_Layout(ST_WIDE_W, ST_WIDE_H, FALSE, &layout);
	if (layout.planar_stride <= 0) {
		printf("  ST16 convert wide: FAIL (layout)\n");
		return 1;
	}

	for (int i = 0; i < ST_WIDE_CHUNKY; ++i) {
		const int x = i % ST_WIDE_W;
		const int y = i / ST_WIDE_W;
		chunky[i] = (uint8_t)(2 + (((x * 3 + y * 11) ^ i) & 15));
	}

	blob_size = (size_t)ST16_ICONTROL_SIZE + (size_t)ST_WIDE_CHUNKY;
	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 convert wide: FAIL (alloc)\n");
		return 1;
	}
	memset(blob, 0, blob_size);
	ST16_Write_LE16(blob + 0, ST_WIDE_W);
	ST16_Write_LE16(blob + 2, ST_WIDE_H);
	ST16_Write_LE16(blob + 4, ST_WIDE_ICON_COUNT);
	ST16_Write_LE32(blob + 8, (uint32_t)blob_size);
	ST16_Write_LE32(blob + 12, ST16_ICONS_STANDARD);
	memcpy(blob + ST16_ICONTROL_SIZE, chunky, sizeof(chunky));

	if (!ST16_Iconset_Should_Convert(blob, blob_size)) {
		fails++;
		printf("  FAIL wide: Should_Convert\n");
	}

	st16_bake_reference_icon(ref_planar, chunky, ST_WIDE_W, ST_WIDE_H, &layout);

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		fails++;
		printf("  FAIL wide: Convert_InPlace\n");
		free(blob);
		return fails;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	if (!ST16_Is_Native(blob, new_size) || !ST16_Validate(blob, new_size)) {
		fails++;
		printf("  FAIL wide: native validate\n");
	}

	{
		const uint8_t *got = ST16_Planar_Icon_Ptr(blob, new_size, 0, &layout);
		if (!got || st16_report_slab_diff(got, ref_planar, (size_t)layout.planar_stride, 0)) {
			fails++;
		}
	}

	free(blob);
	if (fails == 0) {
		printf("  ST16 convert wide: PASS (%dx%d)\n", ST_WIDE_W, ST_WIDE_H);
	}
	return fails;
}

enum {
	ST_D01_MAP_COUNT = 4,
	ST_D01_IMAGE_COUNT = 3
};

static int st16_run_d01_map_count_check(void)
{
	ST16_PlanarLayout layout;
	uint8_t *blob = NULL;
	uint8_t scratch[ST_TILE_CHUNKY];
	uint8_t ref_planar[ST_D01_IMAGE_COUNT * 384];
	size_t const icons_bytes = (size_t)ST_D01_IMAGE_COUNT * (size_t)ST_TILE_CHUNKY;
	size_t const map_off = (size_t)ST16_ICONTROL_SIZE + icons_bytes;
	size_t const transflag_off = map_off + (size_t)ST_D01_MAP_COUNT;
	size_t const blob_size = transflag_off + (size_t)ST_D01_MAP_COUNT;
	uint8_t const map_before[ST_D01_MAP_COUNT] = {0xFF, 0, 1, 2};
	size_t new_size;
	int fails = 0;
	int i;
	int image_index;

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, FALSE, &layout);
	if (layout.planar_stride <= 0) {
		printf("  ST16 convert D01 map: FAIL (layout)\n");
		return 1;
	}

	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 convert D01 map: FAIL (alloc)\n");
		return 1;
	}
	memset(blob, 0, blob_size);
	ST16_Write_LE16(blob + 0, ST_TILE_W);
	ST16_Write_LE16(blob + 2, ST_TILE_H);
	ST16_Write_LE16(blob + 4, ST_D01_MAP_COUNT);
	ST16_Write_LE32(blob + 8, (uint32_t)blob_size);
	ST16_Write_LE32(blob + 12, ST16_ICONS_STANDARD);
	ST16_Write_LE32(blob + 28, (uint32_t)map_off);
	ST16_Write_LE32(blob + 24, (uint32_t)transflag_off);
	for (i = 0; i < (int)icons_bytes; ++i) {
		blob[ST16_ICONTROL_SIZE + i] = (uint8_t)(3 + (i & 15));
	}
	memcpy(blob + map_off, map_before, sizeof(map_before));

	if (!ST16_Iconset_Should_Convert(blob, blob_size)) {
		fails++;
		printf("  FAIL D01 map: Should_Convert\n");
	}
	{
		ST16_IControlView ic;
		size_t image_count;
		if (!ST16_Parse_IControl(blob, blob_size, &ic)) {
			fails++;
			printf("  FAIL D01 map: Parse_IControl\n");
		} else {
			image_count = ST16_Icon_Image_Count(blob, blob_size, &ic);
			if (image_count != ST_D01_IMAGE_COUNT) {
				fails++;
				printf("  FAIL D01 map: image_count %u (want %d)\n",
					(unsigned)image_count,
					ST_D01_IMAGE_COUNT);
			}
		}
	}

	for (i = 0; i < ST_D01_IMAGE_COUNT; ++i) {
		st16_bake_reference_icon(
			ref_planar + (size_t)i * (size_t)layout.planar_stride,
			blob + ST16_ICONTROL_SIZE + (size_t)i * (size_t)ST_TILE_CHUNKY,
			ST_TILE_W,
			ST_TILE_H,
			&layout);
	}

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		printf("  FAIL D01 map: Convert_InPlace\n");
		free(blob);
		return 1;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	if (!ST16_Is_Native(blob, new_size) || !ST16_Validate(blob, new_size)) {
		fails++;
		printf("  FAIL D01 map: native validate\n");
	}

	{
		size_t const planar_bytes =
			(size_t)ST_D01_IMAGE_COUNT * (size_t)layout.planar_stride;
		size_t const map_new = (size_t)ST16_ICONS_V1 + planar_bytes;
		if (map_new + sizeof(map_before) > new_size) {
			fails++;
			printf("  FAIL D01 map: map offset bounds\n");
		} else if (memcmp(blob + map_new, map_before, sizeof(map_before)) != 0) {
			fails++;
			printf("  FAIL D01 map: map bytes corrupted\n");
		}
	}

	for (i = 0; i < ST_D01_IMAGE_COUNT; ++i) {
		const uint8_t *got = ST16_Planar_Icon_Ptr(blob, new_size, i, &layout);
		const uint8_t *ref = ref_planar + (size_t)i * (size_t)layout.planar_stride;
		if (!got || st16_report_slab_diff(got, ref, (size_t)layout.planar_stride, i)) {
			fails++;
		}
	}

	if (ST16_Resolve_Icon_Index(blob, new_size, 0, &image_index)) {
		fails++;
		printf("  FAIL D01 map: resolve logical 0 (0xFF)\n");
	}
	for (i = 1; i < ST_D01_MAP_COUNT; ++i) {
		if (!ST16_Resolve_Icon_Index(blob, new_size, i, &image_index)
			|| image_index != i - 1) {
			fails++;
			printf("  FAIL D01 map: resolve logical %d\n", i);
		}
	}

	free(blob);
	if (fails == 0) {
		printf("  ST16 convert D01 map: PASS (count=%d images=%d)\n",
			ST_D01_MAP_COUNT,
			ST_D01_IMAGE_COUNT);
	}
	return fails;
}

enum {
	ST_TF_IMAGE_COUNT = 6,
	ST_TF_MAP_COUNT = 9
};

static int st16_run_transflag_region_image_count_check(void)
{
	ST16_PlanarLayout layout;
	uint8_t *blob = NULL;
	uint8_t scratch[ST_TILE_CHUNKY];
	size_t const icons_bytes = (size_t)ST_TF_IMAGE_COUNT * (size_t)ST_TILE_CHUNKY;
	size_t const transflag_off = (size_t)ST16_ICONTROL_SIZE + icons_bytes;
	size_t const map_off = transflag_off + (size_t)ST_TF_MAP_COUNT;
	size_t const blob_size = map_off + (size_t)ST_TF_MAP_COUNT;
	size_t new_size;
	int fails = 0;
	int i;
	int image_index;

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, FALSE, &layout);
	if (layout.planar_stride <= 0) {
		printf("  ST16 convert transflag region: FAIL (layout)\n");
		return 1;
	}

	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 convert transflag region: FAIL (alloc)\n");
		return 1;
	}
	memset(blob, 0, blob_size);
	ST16_Write_LE16(blob + 0, ST_TILE_W);
	ST16_Write_LE16(blob + 2, ST_TILE_H);
	ST16_Write_LE16(blob + 4, ST_TF_MAP_COUNT);
	ST16_Write_LE32(blob + 8, (uint32_t)blob_size);
	ST16_Write_LE32(blob + 12, ST16_ICONS_STANDARD);
	ST16_Write_LE32(blob + 24, (uint32_t)transflag_off);
	ST16_Write_LE32(blob + 28, (uint32_t)map_off);
	for (i = 0; i < (int)icons_bytes; ++i) {
		blob[ST16_ICONTROL_SIZE + i] = (uint8_t)(5 + (i & 15));
	}
	for (i = 0; i < ST_TF_MAP_COUNT; ++i) {
		blob[map_off + (size_t)i] = (uint8_t)(i % ST_TF_IMAGE_COUNT);
	}

	{
		ST16_IControlView ic;
		size_t image_count;

		if (!ST16_Parse_IControl(blob, blob_size, &ic)) {
			fails++;
			printf("  FAIL transflag region: Parse_IControl\n");
		} else {
			image_count = ST16_Icon_Image_Count(blob, blob_size, &ic);
			if (image_count != ST_TF_IMAGE_COUNT) {
				fails++;
				printf("  FAIL transflag region: image_count %u (want %d)\n",
					(unsigned)image_count,
					ST_TF_IMAGE_COUNT);
			}
		}
	}

	if (!ST16_Iconset_Should_Convert(blob, blob_size)) {
		fails++;
		printf("  FAIL transflag region: Should_Convert\n");
	}

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		printf("  FAIL transflag region: Convert_InPlace\n");
		free(blob);
		return 1;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	{
		ST16_IControlView ic;
		size_t image_count;

		if (!ST16_Parse_IControl(blob, new_size, &ic)) {
			fails++;
			printf("  FAIL transflag region native: Parse_IControl\n");
		} else {
			image_count = ST16_Icon_Image_Count(blob, new_size, &ic);
			if (image_count != ST_TF_IMAGE_COUNT) {
				fails++;
				printf("  FAIL transflag region native: image_count %u (want %d)\n",
					(unsigned)image_count,
					ST_TF_IMAGE_COUNT);
			}
		}
	}

	for (i = 0; i < ST_TF_MAP_COUNT; ++i) {
		if (!ST16_Resolve_Icon_Index(blob, new_size, i, &image_index)
			|| image_index != (i % ST_TF_IMAGE_COUNT)) {
			fails++;
			printf("  FAIL transflag region: resolve logical %d\n", i);
		}
	}

	free(blob);
	if (fails == 0) {
		printf("  ST16 convert transflag region: PASS (count=%d images=%d)\n",
			ST_TF_MAP_COUNT,
			ST_TF_IMAGE_COUNT);
	}
	return fails;
}

enum {
	ST_S14_IMAGE_COUNT = 3,
	ST_S14_MAP_COUNT = 4
};

static int st16_run_s14_transflag_tail_check(void)
{
	ST16_PlanarLayout layout;
	uint8_t *blob = NULL;
	uint8_t scratch[ST_TILE_CHUNKY];
	uint8_t ref_planar[ST_S14_IMAGE_COUNT * 384];
	size_t const icons_bytes = (size_t)ST_S14_IMAGE_COUNT * (size_t)ST_TILE_CHUNKY;
	size_t const map_off = (size_t)ST16_ICONTROL_SIZE + icons_bytes;
	size_t const transflag_off = map_off + (size_t)ST_S14_MAP_COUNT;
	size_t const blob_size = transflag_off + (size_t)ST_S14_IMAGE_COUNT;
	uint8_t const map_before[ST_S14_MAP_COUNT] = {0, 1, 2, 0xFF};
	size_t new_size;
	int fails = 0;
	int i;

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, FALSE, &layout);
	if (layout.planar_stride <= 0) {
		printf("  ST16 convert S14 tail: FAIL (layout)\n");
		return 1;
	}

	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 convert S14 tail: FAIL (alloc)\n");
		return 1;
	}
	memset(blob, 0, blob_size);
	ST16_Write_LE16(blob + 0, ST_TILE_W);
	ST16_Write_LE16(blob + 2, ST_TILE_H);
	ST16_Write_LE16(blob + 4, ST_S14_MAP_COUNT);
	ST16_Write_LE32(blob + 8, (uint32_t)blob_size);
	ST16_Write_LE32(blob + 12, ST16_ICONS_STANDARD);
	ST16_Write_LE32(blob + 28, (uint32_t)map_off);
	ST16_Write_LE32(blob + 24, (uint32_t)transflag_off);
	for (i = 0; i < (int)icons_bytes; ++i) {
		blob[ST16_ICONTROL_SIZE + i] = (uint8_t)(9 + (i & 15));
	}
	memcpy(blob + map_off, map_before, sizeof(map_before));

	if (!ST16_Iconset_Should_Convert(blob, blob_size)) {
		fails++;
		printf("  FAIL S14 tail: Should_Convert\n");
	}

	for (i = 0; i < ST_S14_IMAGE_COUNT; ++i) {
		st16_bake_reference_icon(
			ref_planar + (size_t)i * (size_t)layout.planar_stride,
			blob + ST16_ICONTROL_SIZE + (size_t)i * (size_t)ST_TILE_CHUNKY,
			ST_TILE_W,
			ST_TILE_H,
			&layout);
	}

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		printf("  FAIL S14 tail: Convert_InPlace\n");
		free(blob);
		return 1;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	if (!ST16_Is_Native(blob, new_size) || !ST16_Validate(blob, new_size)) {
		fails++;
		printf("  FAIL S14 tail: native validate\n");
	}

	for (i = 0; i < ST_S14_IMAGE_COUNT; ++i) {
		const uint8_t *got = ST16_Planar_Icon_Ptr(blob, new_size, i, &layout);
		const uint8_t *ref = ref_planar + (size_t)i * (size_t)layout.planar_stride;
		if (!got || st16_report_slab_diff(got, ref, (size_t)layout.planar_stride, i)) {
			fails++;
		}
	}

	free(blob);
	if (fails == 0) {
		printf("  ST16 convert S14 tail: PASS (count=%d images=%d trans=%d)\n",
			ST_S14_MAP_COUNT,
			ST_S14_IMAGE_COUNT,
			ST_S14_IMAGE_COUNT);
	}
	return fails;
}

static size_t st16_build_masked_blob(
	uint8_t *out,
	size_t cap,
	const uint8_t *chunky_icons,
	const uint8_t *transflags)
{
	const size_t icons_bytes = (size_t)ST_ICON_COUNT * (size_t)ST_TILE_CHUNKY;
	const size_t trans_bytes = (size_t)ST_ICON_COUNT;
	const size_t icons_off = (size_t)ST16_ICONTROL_SIZE;
	const size_t trans_off = icons_off + icons_bytes;
	const size_t total = trans_off + trans_bytes;

	if (!out || cap < total || !chunky_icons || !transflags) {
		return 0;
	}

	memset(out, 0, total);
	ST16_Write_LE16(out + 0, ST_TILE_W);
	ST16_Write_LE16(out + 2, ST_TILE_H);
	ST16_Write_LE16(out + 4, ST_ICON_COUNT);
	ST16_Write_LE32(out + 8, (uint32_t)total);
	ST16_Write_LE32(out + 12, ST16_ICONS_STANDARD);
	ST16_Write_LE32(out + 24, (uint32_t)trans_off);
	memcpy(out + icons_off, chunky_icons, icons_bytes);
	memcpy(out + trans_off, transflags, trans_bytes);
	return total;
}

static int st16_run_masked_convert_check(void)
{
	ST16_PlanarLayout layout;
	uint8_t chunky_icons[ST_ICON_COUNT * ST_TILE_CHUNKY];
	uint8_t transflags[ST_ICON_COUNT] = { 1, 0 };
	uint8_t ref_planar[ST_ICON_COUNT * 384];
	uint8_t ref_mask[ST_ICON_COUNT * 96];
	uint8_t scratch[ST_TILE_CHUNKY];
	uint8_t *blob = NULL;
	size_t blob_size;
	size_t new_size;
	uint16_t flags = 0;
	int fails = 0;
	int i;

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, TRUE, &layout);
	if (layout.planar_stride <= 0 || layout.mask_stride <= 0) {
		printf("  ST16 masked convert: FAIL (layout)\n");
		return 1;
	}
	if (!C2P_Weights_Are_Ready()) {
		return 0;
	}

	st16_fill_synthetic_chunky(chunky_icons, 77);
	chunky_icons[ST_TILE_W * (ST_TILE_H / 2) + (ST_TILE_W / 2)] = 0;

	blob_size = (size_t)ST16_ICONTROL_SIZE
		+ (size_t)ST_ICON_COUNT * (size_t)ST_TILE_CHUNKY
		+ (size_t)ST_ICON_COUNT;
	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 masked convert: FAIL (alloc)\n");
		return 1;
	}
	if (st16_build_masked_blob(blob, blob_size, chunky_icons, transflags) != blob_size) {
		fails++;
		printf("  FAIL masked: build blob\n");
	}
	if (!ST16_Iconset_Should_Convert(blob, blob_size) || !ST16_Iconset_Uses_Mask(blob, blob_size)) {
		fails++;
		printf("  FAIL masked: Should_Convert / Uses_Mask\n");
	}

	for (i = 0; i < ST_ICON_COUNT; ++i) {
		const uint8_t *const icon_chunky = chunky_icons + (size_t)i * (size_t)ST_TILE_CHUNKY;
		const BOOL per_pixel_trans = transflags[i] != 0;

		st16_bake_reference_icon(
			ref_planar + (size_t)i * (size_t)layout.planar_stride,
			icon_chunky,
			ST_TILE_W,
			ST_TILE_H,
			&layout);
		memset(ref_mask + (size_t)i * (size_t)layout.mask_stride, 0, (size_t)layout.mask_stride);
		ST16_Build_Mask_From_Chunky(
			icon_chunky,
			ST_TILE_W,
			ST_TILE_H,
			per_pixel_trans,
			ref_mask + (size_t)i * (size_t)layout.mask_stride,
			&layout);
	}

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		fails++;
		printf("  FAIL masked: Convert_InPlace\n");
		free(blob);
		return fails;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	if (!ST16_Is_Native(blob, new_size) || !ST16_Validate(blob, new_size)) {
		fails++;
		printf("  FAIL masked: native validate\n");
	}
	if (!ST16_Read_Chunk_Flags(blob, new_size, &flags) || !(flags & ST16_FLAG_HAS_MASK)) {
		fails++;
		printf("  FAIL masked: HAS_MASK flag\n");
	}

	for (i = 0; i < ST_ICON_COUNT; ++i) {
		const uint8_t *got_planar = ST16_Planar_Icon_Ptr(blob, new_size, i, &layout);
		const uint8_t *got_mask;
		const uint8_t *ref_p = ref_planar + (size_t)i * (size_t)layout.planar_stride;
		const uint8_t *ref_m = ref_mask + (size_t)i * (size_t)layout.mask_stride;

		if (!got_planar || layout.planar_stride <= 0 || layout.mask_stride <= 0) {
			fails++;
			printf("  FAIL masked icon %d: planar ptr/layout\n", i);
			continue;
		}
		got_mask = got_planar + (size_t)layout.planar_stride;
		if (st16_report_slab_diff(got_planar, ref_p, (size_t)layout.planar_stride, i)) {
			fails++;
		}
		if (st16_report_slab_diff(got_mask, ref_m, (size_t)layout.mask_stride, i)) {
			fails++;
			printf("  (masked slab icon %d)\n", i);
		}
	}

	free(blob);
	if (fails == 0) {
		printf("  ST16 masked convert: PASS\n");
	}
	return fails;
}

int st_run_st16_convert_autotest_ex(int verbose, int *out_failures)
{
	ST16_PlanarLayout layout;
	uint8_t chunky_icons[ST_ICON_COUNT * ST_TILE_CHUNKY];
	uint8_t ref_planar[ST_ICON_COUNT * 384];
	uint8_t scratch[ST_TILE_CHUNKY];
	uint8_t *blob = NULL;
	size_t blob_size;
	size_t new_size;
	int fails = 0;
	int i;

	if (out_failures) {
		*out_failures = 0;
	}

	ST16_Compute_Planar_Layout(ST_TILE_W, ST_TILE_H, FALSE, &layout);
	if (layout.planar_stride <= 0) {
		printf("  ST16 convert: FAIL (layout)\n");
		return 1;
	}

	C2P_Load_WeightSet("TEMPERAT", "ST16 convert autotest");
	{
		unsigned char pal[768];
		memcpy(pal, kStTemperatPal768, sizeof(pal));
		Set_Palette(pal);
	}
	if (!C2P_Weights_Are_Ready()) {
		printf("  ST16 convert: SKIP (no C2P weights)\n");
		return 0;
	}

	st16_fill_synthetic_chunky(chunky_icons, 41);
	blob_size = (size_t)ST16_ICONTROL_SIZE + (size_t)ST_ICON_COUNT * (size_t)ST_TILE_CHUNKY;
	blob = (uint8_t *)malloc(blob_size);
	if (!blob) {
		printf("  ST16 convert: FAIL (alloc)\n");
		return 1;
	}
	if (st16_build_standard_blob(blob, blob_size, chunky_icons) != blob_size) {
		fails++;
		printf("  FAIL: build blob\n");
	}

	if (!ST16_Iconset_Should_Convert(blob, blob_size)) {
		fails++;
		printf("  FAIL: Should_Convert\n");
	}

	for (i = 0; i < ST_ICON_COUNT; ++i) {
		st16_bake_reference_icon(
			ref_planar + (size_t)i * (size_t)layout.planar_stride,
			chunky_icons + (size_t)i * (size_t)ST_TILE_CHUNKY,
			ST_TILE_W,
			ST_TILE_H,
			&layout);
	}

	if (!ST16_Convert_InPlace(blob, blob_size, scratch)) {
		fails++;
		printf("  FAIL: Convert_InPlace\n");
		free(blob);
		if (out_failures) {
			*out_failures = fails;
		}
		return fails;
	}

	new_size = (size_t)ST16_Read_LE32(blob + 8);
	if (!ST16_Is_Native(blob, new_size) || !ST16_Validate(blob, new_size)) {
		fails++;
		printf("  FAIL: native validate\n");
	}

	for (i = 0; i < ST_ICON_COUNT; ++i) {
		const uint8_t *got = ST16_Planar_Icon_Ptr(blob, new_size, i, &layout);
		const uint8_t *ref = ref_planar + (size_t)i * (size_t)layout.planar_stride;

		if (!got) {
			fails++;
			printf("  FAIL icon %d: Planar_Icon_Ptr NULL\n", i);
			continue;
		}
		if (st16_report_slab_diff(
				got,
				ref,
				(size_t)layout.planar_stride,
				i)) {
			fails++;
		} else if (verbose) {
			printf("  OK icon %d slab\n", i);
		}
	}

	free(blob);

	fails += st16_run_wide_convert_check();
	fails += st16_run_d01_map_count_check();
	fails += st16_run_transflag_region_image_count_check();
	fails += st16_run_s14_transflag_tail_check();
	fails += st16_run_clear1_tail_overlap_check();
	fails += st16_run_masked_convert_check();

	if (out_failures) {
		*out_failures = fails;
	}

	if (fails == 0) {
		printf("  ST16 convert: PASS\n");
		return 0;
	}

	printf("  ST16 convert: FAIL (%d checks)\n", fails);
	return fails;
}

int st_run_st16_convert_autotest(void)
{
	return st_run_st16_convert_autotest_ex(0, NULL);
}
