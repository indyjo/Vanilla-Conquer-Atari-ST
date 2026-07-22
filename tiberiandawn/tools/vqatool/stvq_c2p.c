/*
 * stvq_c2p.c - 2x2 Bayer + planar pack for STVQ encode.
 */
#include "stvq_c2p.h"

#include <stddef.h>
#include <string.h>

/* Ranks 0..15 for weight rows summing to 16 (granularity 4 / 2x2). */
static const uint8_t Bayer2x2[4] = {0, 8, 12, 4};

static uint8_t map_from_weights(const uint8_t *w_row, int phase)
{
	int rank = (int)Bayer2x2[phase & 3];
	int cum = 0;
	int k;
	for (k = 0; k < 16; k++) {
		cum += (int)w_row[k];
		if (rank < cum)
			return (uint8_t)k;
	}
	return 15;
}

void stvq_c2p_init(StvqC2P *c2p, const StvqWeightSet *w16)
{
	int phase, src;
	for (phase = 0; phase < 4; phase++) {
		for (src = 0; src < 256; src++) {
			c2p->map[phase][src] = map_from_weights(w16->weights[src], phase);
		}
	}
}

uint8_t stvq_c2p_map(const StvqC2P *c2p, int x, int y, uint8_t pal_idx)
{
	int phase = ((y & 1) << 1) | (x & 1);
	return c2p->map[phase][pal_idx];
}

void stvq_pack_tile_32(const uint8_t chunky8x8[64], uint8_t out32[32])
{
	int y;
	memset(out32, 0, 32);
	for (y = 0; y < 8; y++) {
		uint8_t p0 = 0, p1 = 0, p2 = 0, p3 = 0;
		int x;
		for (x = 0; x < 8; x++) {
			uint8_t c = chunky8x8[y * 8 + x] & 0x0fu;
			uint8_t bit = (uint8_t)(1u << (7 - x));
			if (c & 1)
				p0 |= bit;
			if (c & 2)
				p1 |= bit;
			if (c & 4)
				p2 |= bit;
			if (c & 8)
				p3 |= bit;
		}
		/* movep.l store order: plane0,1,2,3 as successive bytes at +0,+2,+4,+6 from column —
		   for a packed 32-byte tile we store 4 plane bytes per scanline. */
		out32[y * 4 + 0] = p0;
		out32[y * 4 + 1] = p1;
		out32[y * 4 + 2] = p2;
		out32[y * 4 + 3] = p3;
	}
}

void stvq_unpack_tile_32(const uint8_t tile32[32], uint8_t chunky8x8[64])
{
	int y, x;
	for (y = 0; y < 8; y++) {
		uint8_t p0 = tile32[y * 4 + 0];
		uint8_t p1 = tile32[y * 4 + 1];
		uint8_t p2 = tile32[y * 4 + 2];
		uint8_t p3 = tile32[y * 4 + 3];
		for (x = 0; x < 8; x++) {
			uint8_t bit = (uint8_t)(1u << (7 - x));
			uint8_t c = 0;
			if (p0 & bit)
				c |= 1u;
			if (p1 & bit)
				c |= 2u;
			if (p2 & bit)
				c |= 4u;
			if (p3 & bit)
				c |= 8u;
			chunky8x8[y * 8 + x] = c;
		}
	}
}

int stvq_frame_to_tiles(const StvqC2P *c2p, const uint8_t *vga_pixels, unsigned width, unsigned height,
    unsigned tiles_x, unsigned tiles_y, uint8_t *out_tiles, uint8_t *out_src)
{
	unsigned col, row;
	if (!width || !height)
		return -1;
	for (col = 0; col < tiles_x; col++) {
		for (row = 0; row < tiles_y; row++) {
			uint8_t chunky_pen[64];
			uint8_t chunky_src[64];
			int ly, lx;
			size_t ti = (size_t)col * tiles_y + row;
			for (ly = 0; ly < 8; ly++) {
				for (lx = 0; lx < 8; lx++) {
					unsigned x = col * 8u + (unsigned)lx;
					unsigned y = row * 8u + (unsigned)ly;
					/* Extend edge color into pad (avoid black fringe in half tiles). */
					unsigned sx = x < width ? x : width - 1u;
					unsigned sy = y < height ? y : height - 1u;
					uint8_t src = vga_pixels[sy * width + sx];
					uint8_t pen = 0;
					if (c2p)
						pen = stvq_c2p_map(c2p, (int)x, (int)y, src);
					chunky_src[ly * 8 + lx] = src;
					chunky_pen[ly * 8 + lx] = pen;
				}
			}
			if (out_tiles)
				stvq_pack_tile_32(chunky_pen, out_tiles + ti * 32u);
			if (out_src)
				memcpy(out_src + ti * 64u, chunky_src, 64);
		}
	}
	return 0;
}
