/*
 * weight_opt.h - Per-index integer dither weights (sum 16, <=4 nonzero pens).
 * Objective: (1-lambda)*e1 + lambda*e2 with Bayer tile (matches C2P MapDither).
 *
 * palette_opt_weight_granularity: each pen weight is a multiple of this value (default 1).
 * Use 4 for 2x2 Bayer (four slots in the 16-weight encoding); 1 for full 4x4 resolution.
 */

#ifndef PALETTE_OPT_WEIGHT_OPT_H
#define PALETTE_OPT_WEIGHT_OPT_H

#define PALETTE_OPT_WEIGHT_SLOTS 16
#define PALETTE_OPT_WEIGHT_SUM 16
#define PALETTE_OPT_MAX_NONZERO_WEIGHTS 4
#define PALETTE_OPT_WEIGHT_GRANULARITY_2X2_BAYER 4

extern int palette_opt_weight_granularity;

/*
 * colors: 768 floats (metric space, caller-defined).
 * subset: n palette indices; pen k uses colors[3*subset[k]].
 * target_i: source palette index 0..255.
 * out_weights[16]: pen-slot weights summing to PALETTE_OPT_WEIGHT_SUM.
 * Returns blended cost c = (1-lambda)*e1 + lambda*e2, or -1 if infeasible.
 */
float palette_weight_opt_best(const float *colors, const float *dist_sq,
	const unsigned char *subset, int subset_n, int target_i, float lambda,
	unsigned char *out_weights);

void palette_weight_e1_e2(const float *colors, const float *dist_sq, const unsigned char *subset,
	int subset_n, int target_i, const unsigned char *weights, float lambda, float *out_e1,
	float *out_e2, float *out_blend);

#endif /* PALETTE_OPT_WEIGHT_OPT_H */
