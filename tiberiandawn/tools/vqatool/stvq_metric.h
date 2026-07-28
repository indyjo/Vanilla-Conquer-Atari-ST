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
#define STVQ_DEFAULT_GAMMA 0.77f

/*
 * DCT coeff weight: w(u,v) = 1 / (1 + alpha*(u^2+v^2)).
 * Features store sqrt(w)*coeff so L2 matches weighted SSE.
 * Layout: [Y₀..Y_{ny-1} | U₀..U_{nu-1} | V₀..V_{nv-1}],
 * feat_len = ny + 2*nu (each plane count clamped to 0..64).
 * Fewer chroma coeffs underweight U/V vs Y (chroma is smoother;
 * U/V magnitudes are already smaller than Y).
 */
void stvq_metric_set_dct(float alpha, unsigned y_coeffs, unsigned chroma_coeffs);
float stvq_metric_dct_alpha(void);
unsigned stvq_metric_dct_coeffs(void);        /* Y count */
unsigned stvq_metric_dct_chroma_coeffs(void); /* U and V each */
unsigned stvq_metric_feat_len(void);          /* ny + 2*nu */

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

/* SSE of features, scaled. */
unsigned stvq_metric_feat_dist(const float *a, const float *b);
unsigned stvq_metric_feat_dist_lim(const float *a, const float *b, unsigned max_d);

/* Convenience: build feats and compare (src VGA indices vs pens / planar tile). */
unsigned stvq_src_pens_error(const uint8_t src_vga[64], const uint8_t pens[64]);
unsigned stvq_src_pens_error_lim(const uint8_t src_vga[64], const uint8_t pens[64], unsigned max_d);
unsigned stvq_src_tile_error(const uint8_t src_vga[64], const uint8_t tile32[32]);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_METRIC_H */
