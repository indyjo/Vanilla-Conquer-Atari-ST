/*
 * stvq_metric.h - Weighted DCT tile metric (YUV → DCT → C coeffs).
 */
#ifndef STVQ_METRIC_H
#define STVQ_METRIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-point scale for unsigned tile error: round(sum_sq * SCALE). */
#define STVQ_METRIC_SCALE 65536.0f

#define STVQ_METRIC_MAX_COEFFS 64
#define STVQ_DEFAULT_DCT_ALPHA 0.2f
#define STVQ_DEFAULT_DCT_COEFFS 15
#define STVQ_DEFAULT_GAMMA 0.77f
#define STVQ_DEFAULT_Y_SCALE 2.0f

/*
 * DCT coeff weight: w(u,v) = 1 / (1 + alpha*(u^2+v^2)).
 * Features store sqrt(w)*coeff so L2 matches weighted SSE.
 */
void stvq_metric_set_dct(float alpha, unsigned ncoeffs);
float stvq_metric_dct_alpha(void);
unsigned stvq_metric_dct_coeffs(void);

/* Palette-opt YUV transform params (applied on next set_palette). */
void stvq_metric_set_color(float gamma, float y_scale);
float stvq_metric_gamma(void);
float stvq_metric_y_scale(void);

/*
 * Build VGA YUV table + pen→VGA map from full VGA6 palette + W16 subset.
 * Uses current gamma / y_scale (defaults 0.77 / 2.0).
 */
void stvq_metric_set_palette_vga6(const uint8_t pal768[768], const uint8_t subset[16]);

/* Y-plane zig-zag DCT features (ncoeffs). idx[64] = VGA or mapped pen colors. */
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
