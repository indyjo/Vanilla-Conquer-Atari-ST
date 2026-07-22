/*
 * stvq_c2p.h - Host 2x2 Bayer dither + 8x8 planar tile pack.
 */
#ifndef STVQ_C2P_H
#define STVQ_C2P_H

#include "stvq_palette.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqC2P {
	uint8_t map[4][256]; /* (y&1)*2+(x&1) -> ST pen */
} StvqC2P;

void stvq_c2p_init(StvqC2P *c2p, const StvqWeightSet *w16);
uint8_t stvq_c2p_map(const StvqC2P *c2p, int x, int y, uint8_t pal_idx);

/* Pack 8x8 chunky 4bpp pens into 32-byte ST interleaved planar (movep order). */
void stvq_pack_tile_32(const uint8_t chunky8x8[64], uint8_t out32[32]);
void stvq_unpack_tile_32(const uint8_t tile32[32], uint8_t chunky8x8[64]);

/*
 * Rasterize one frame to planar 16-pen tiles (W16 dither) and/or raw VGA src tiles.
 * tiles_x/y = ceil dims; visible width/height.
 * Pixels past the visible edge clamp to the nearest covered pixel (no black pad).
 * Bayer phase still uses the absolute tile (x,y).
 * Layout: column-major (col major, row within col).
 * out_tiles: tiles_x * tiles_y * 32 (may be NULL).
 * out_src:   tiles_x * tiles_y * 64 VGA indices (may be NULL).
 */
int stvq_frame_to_tiles(const StvqC2P *c2p, const uint8_t *vga_pixels, unsigned width, unsigned height,
    unsigned tiles_x, unsigned tiles_y, uint8_t *out_tiles, uint8_t *out_src);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_C2P_H */
