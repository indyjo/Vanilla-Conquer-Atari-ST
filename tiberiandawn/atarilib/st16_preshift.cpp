/*
 * st16_preshift.cpp - build the 16 shifted variants of a terrain tile.
 */

#include "st16_preshift.h"

#include <string.h>

/*
 * Source rows are stored a whole number of words wide, so the bits past src_w
 * are undefined. Force them so the output is fully defined: 0 for planar (no
 * pixel), 1 for a mask (no clearing).
 */
static inline uint16_t shp_src_word(const uint8_t *row, int col, int byte_stride,
	int plane_off, int src_w, int src_cols, uint16_t outside)
{
	if (col < 0 || col >= src_cols) {
		return outside;
	}

	uint16_t word = *(const uint16_t *)(row + (size_t)col * (size_t)byte_stride + (size_t)plane_off);
	int const valid = src_w - col * 16;
	if (valid < 16) {
		uint16_t const keep = (uint16_t)(0xFFFFu << (16 - valid));
		word = (uint16_t)((word & keep) | (outside & (uint16_t)~keep));
	}
	return word;
}

/*
 * Both routines walk destination words and pull from the two source words that
 * can contribute. Reading past the source yields 0 (planar) or all ones (mask),
 * which is why the fill value differs between them.
 */

void ST16_Preshift_Planar(const uint8_t *src,
	int src_row_bytes,
	int src_w,
	int rows,
	uint8_t *dst,
	int dst_row_bytes,
	int shift)
{
	if (!src || !dst || src_w <= 0 || rows <= 0 || shift < 0 || shift > 15) {
		return;
	}

	int const src_cols = (src_w + 15) >> 4;
	int const dst_cols = (src_w + shift + 15) >> 4;

	for (int row = 0; row < rows; ++row) {
		const uint8_t *const s = src + (size_t)row * (size_t)src_row_bytes;
		uint8_t *const d = dst + (size_t)row * (size_t)dst_row_bytes;

		for (int plane = 0; plane < 4; ++plane) {
			for (int col = 0; col < dst_cols; ++col) {
				/* Destination word col takes the tail of source col-1 and the
				 * head of source col. */
				uint16_t const hi = shp_src_word(s, col - 1, 8, plane * 2, src_w, src_cols, 0u);
				uint16_t const lo = shp_src_word(s, col, 8, plane * 2, src_w, src_cols, 0u);

				uint32_t const pair = ((uint32_t)hi << 16) | (uint32_t)lo;
				uint16_t const out = (uint16_t)(pair >> shift);
				*(uint16_t *)(d + (size_t)col * 8u + (size_t)plane * 2u) = out;
			}
		}
	}
}

void ST16_Preshift_Mask(const uint8_t *src,
	int src_row_bytes,
	int src_w,
	int rows,
	uint8_t *dst,
	int dst_row_bytes,
	int shift)
{
	if (!src || !dst || src_w <= 0 || rows <= 0 || shift < 0 || shift > 15) {
		return;
	}

	int const src_cols = (src_w + 15) >> 4;
	int const dst_cols = (src_w + shift + 15) >> 4;

	for (int row = 0; row < rows; ++row) {
		const uint8_t *const s = src + (size_t)row * (size_t)src_row_bytes;
		uint8_t *const d = dst + (size_t)row * (size_t)dst_row_bytes;

		for (int col = 0; col < dst_cols; ++col) {
			uint16_t const hi = shp_src_word(s, col - 1, 2, 0, src_w, src_cols, 0xFFFFu);
			uint16_t const lo = shp_src_word(s, col, 2, 0, src_w, src_cols, 0xFFFFu);

			uint32_t const pair = ((uint32_t)hi << 16) | (uint32_t)lo;
			*(uint16_t *)(d + (size_t)col * 2u) = (uint16_t)(pair >> shift);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Cache                                                                      */
/* ------------------------------------------------------------------------- */

#include "memflag.h"
#include "debugstring.h"

/*
 * Only 24x24 is cached, so every slot has the same geometry: a 24 pixel tile
 * shifted by up to 15 spans three words.
 */
enum {
	SHP_TILE_W = 24,
	SHP_TILE_H = 24,
	SHP_COLS = ((SHP_TILE_W + 15 + 15) >> 4), /* 3 */
	SHP_PLANAR_ROW = SHP_COLS * 8,
	SHP_MASK_ROW = SHP_COLS * 2,
	SHP_PLANAR_BYTES = SHP_PLANAR_ROW * SHP_TILE_H,
	SHP_MASK_BYTES = SHP_MASK_ROW * SHP_TILE_H,
	SHP_SLOT_BYTES = SHP_PLANAR_BYTES + SHP_MASK_BYTES
};

/* Memory that must stay free: below the first the table is not built at all,
 * below the second it stays at its 256-slot base. */
#define SHP_MIN_HEADROOM  (2L * 1024L * 1024L)
#define SHP_GROW_HEADROOM (4L * 1024L * 1024L)

/* Direct mapped: a collision just rebuilds, which costs one tile shift. */
struct ShpEntry {
	const uint8_t *src;
	uint8_t shift;
	uint8_t has_mask;
};

static ShpEntry *g_shp_entries = NULL;
static uint8_t *g_shp_slab = NULL;
static int g_shp_slots = 0;
static int g_shp_tried = 0;

static void shp_init_once(void)
{
	if (g_shp_tried) {
		return;
	}
	g_shp_tried = 1;

	/*
	 * Hot set is roughly the visible distinct tiles times the shifts they get
	 * drawn at. Scale with memory, same rule as the other ST caches: a 4 MB STE
	 * keeps the smallest table, a machine with alternate RAM gets a big one.
	 */
	long const free_bytes = Total_Ram_Free(MEM_NORMAL);

	/*
	 * Skip the cache entirely on a tight machine: the skewed blit still works,
	 * and 4 MB has better uses for 180 KB.
	 */
	long const smallest = 256L * (long)(SHP_SLOT_BYTES + (int)sizeof(ShpEntry));
	if (free_bytes < smallest + SHP_MIN_HEADROOM) {
		DBG_INFO("Preshift: off, %ld KiB free", free_bytes / 1024L);
		return;
	}

	int slots = 256;
	while (slots < 4096
	    && (long)(slots * 2) * (long)(SHP_SLOT_BYTES + (int)sizeof(ShpEntry))
	           + SHP_GROW_HEADROOM
	        <= free_bytes) {
		slots *= 2;
	}

	void (*saved)(void) = Memory_Error;
	Memory_Error = NULL;
	g_shp_entries = (ShpEntry *)Alloc((unsigned long)slots * sizeof(ShpEntry), MEM_NORMAL);
	g_shp_slab = g_shp_entries
		? (uint8_t *)Alloc((unsigned long)slots * (unsigned long)SHP_SLOT_BYTES, MEM_NORMAL)
		: NULL;
	Memory_Error = saved;

	if (!g_shp_entries || !g_shp_slab) {
		if (g_shp_slab) {
			Free(g_shp_slab);
			g_shp_slab = NULL;
		}
		if (g_shp_entries) {
			Free(g_shp_entries);
			g_shp_entries = NULL;
		}
		DBG_WARN("Preshift: no memory, tiles stay skewed at blit time");
		return;
	}

	memset(g_shp_entries, 0, (size_t)slots * sizeof(ShpEntry));
	g_shp_slots = slots;
	DBG_INFO("Preshift: %d slots, %ld KiB", slots,
	    ((long)slots * (long)SHP_SLOT_BYTES) / 1024L);
}

void ST16_Preshift_Reset(void)
{
	if (g_shp_entries && g_shp_slots > 0) {
		memset(g_shp_entries, 0, (size_t)g_shp_slots * sizeof(ShpEntry));
	}
}

int ST16_Preshift_Lookup(const uint8_t *planar,
	int planar_row_bytes,
	const uint8_t *mask,
	int mask_row_bytes,
	int tile_w,
	int tile_h,
	int has_mask,
	int shift,
	ST16_PreshiftView *out)
{
	if (!planar || !out || tile_w != SHP_TILE_W || tile_h != SHP_TILE_H
	    || shift < 0 || shift > 15
	    || (has_mask && (!mask || mask_row_bytes <= 0))) {
		return 0;
	}

	shp_init_once();
	if (g_shp_slots <= 0) {
		return 0;
	}

	uint32_t h = (uint32_t)(uintptr_t)planar;
	h ^= h >> 13;
	h += (uint32_t)shift * 0x9E37u;
	int const slot = (int)(h & (uint32_t)(g_shp_slots - 1));

	ShpEntry *const e = &g_shp_entries[slot];
	uint8_t *const payload = g_shp_slab + (size_t)slot * (size_t)SHP_SLOT_BYTES;

	if (e->src != planar || e->shift != (uint8_t)shift || e->has_mask != (uint8_t)(has_mask != 0)) {
		ST16_Preshift_Planar(planar, planar_row_bytes, tile_w, tile_h, payload,
			SHP_PLANAR_ROW, shift);
		if (has_mask) {
			ST16_Preshift_Mask(mask, mask_row_bytes, tile_w, tile_h,
				payload + SHP_PLANAR_BYTES, SHP_MASK_ROW, shift);
		}
		e->src = planar;
		e->shift = (uint8_t)shift;
		e->has_mask = (uint8_t)(has_mask != 0);
	}

	out->planar = payload;
	out->mask = has_mask ? (payload + SHP_PLANAR_BYTES) : NULL;
	out->planar_row_bytes = SHP_PLANAR_ROW;
	out->mask_row_bytes = SHP_MASK_ROW;
	out->src_x = shift;
	return 1;
}
