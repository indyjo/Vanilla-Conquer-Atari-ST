/*
 * stvq_metric.h - Weighted DCT tile metric (YUV → DCT → feature L2).
 */
#ifndef STVQ_METRIC_H
#define STVQ_METRIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-point scale for unsigned tile error: round(sum_sq * SCALE). */
#define STVQ_METRIC_SCALE 65536.0f

/* Max zig-zag coeffs per Y/U/V plane (8×8 DCT). */
#define STVQ_METRIC_MAX_COEFFS 64
/* Max feature vector length: full Y + U + V packs. */
#define STVQ_METRIC_MAX_FEAT_LEN (STVQ_METRIC_MAX_COEFFS * 3u)
#define STVQ_DEFAULT_DCT_ALPHA 0.2f
#define STVQ_DEFAULT_DCT_COEFFS 15        /* Y zig-zag count */
#define STVQ_DEFAULT_DCT_CHROMA_COEFFS 7  /* U and V each (≈ half of Y) */
/* U/V SSE scale. 0.25 matches palette-opt y_scale 2 (squared). 0 = luma-only. */
#define STVQ_DEFAULT_DCT_CHROMA_WEIGHT 0.25f
#define STVQ_DEFAULT_GAMMA 0.77f

/*
 * DCT coeff weight: w(u,v) = 1 / (1 + alpha*(u^2+v^2)).
 * Kept coeffs store sqrt(w)*C so their L2 is weighted SSE. U and V are also
 * multiplied by sqrt(chroma_weight); chroma_weight 0 zeros every U/V term.
 * Layout: [Y×ny | U×nc | V×nc | E_y? | E_u? | E_v?].
 * A plane truncated below 64 stores one tail energy
 *   E = scale * sum_{i>=n} w_i * C_i^2
 * (scale is 1 for Y and chroma_weight for U/V). A full plane stores nothing
 * (E = 0), so feat_len <= 64*3.
 * Distance is SSE of the kept coeffs plus (E_a + E_b) per tail: the expected
 * squared error when discarded coefficients are uncorrelated. Tail slots are
 * not square-differenced.
 */
void stvq_metric_set_dct(float alpha, unsigned y_coeffs, unsigned chroma_coeffs, float chroma_weight);
float stvq_metric_dct_alpha(void);
unsigned stvq_metric_dct_coeffs(void);        /* Y count */
unsigned stvq_metric_dct_chroma_coeffs(void); /* U and V each */
float stvq_metric_dct_chroma_weight(void);    /* U/V SSE weight; 0 = luma-only */
unsigned stvq_metric_feat_len(void);          /* kept coeffs + tail energies */

/* Palette-opt YUV gamma (applied on next set_palette). Y scale is fixed at 1. */
void stvq_metric_set_gamma(float gamma);
float stvq_metric_gamma(void);

/*
 * Build VGA YUV table + pen→VGA map from full VGA6 palette + W16 subset.
 * Uses current gamma (default 0.77); Y unscaled. No STE bpc quantize — DCT
 * features come from original VGA6 colors; hardware 4-bit is STPL-only.
 */
void stvq_metric_set_palette_vga6(const uint8_t pal768[768], const uint8_t subset[16]);

/* YUV zig-zag DCT features (feat_len). idx[64] = VGA or mapped pen colors. */
void stvq_metric_feat_from_indices(const uint8_t idx[64], float *out_feat);
void stvq_metric_feat_from_pens(const uint8_t pens[64], float *out_feat);
void stvq_metric_feat_from_tile32(const uint8_t tile32[32], float *out_feat);

/* Scaled SSE of kept coeffs, plus E_a+E_b for each stored tail. */
unsigned stvq_metric_feat_dist(const float *a, const float *b);
unsigned stvq_metric_feat_dist_lim(const float *a, const float *b, unsigned max_d);

/* Convenience: build feats and compare (src VGA indices vs pens / planar tile). */
unsigned stvq_src_pens_error(const uint8_t src_vga[64], const uint8_t pens[64]);
unsigned stvq_src_pens_error_lim(const uint8_t src_vga[64], const uint8_t pens[64], unsigned max_d);
unsigned stvq_src_tile_error(const uint8_t src_vga[64], const uint8_t tile32[32]);
/* Both sides are dithered 4bpp tiles (pens mapped through the current subset). */
unsigned stvq_tile_error(const uint8_t a[32], const uint8_t b[32]);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_METRIC_H */
