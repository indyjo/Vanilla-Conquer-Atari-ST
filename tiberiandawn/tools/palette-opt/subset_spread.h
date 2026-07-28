/*
 * subset_spread.h - Spread-maximizing palette subset (farthest-point sampling).
 */

#ifndef PALETTE_OPT_SUBSET_SPREAD_H
#define PALETTE_OPT_SUBSET_SPREAD_H

#include "subset_fix.h"

struct PaletteOptJsonExport;

#define PALETTE_SUBSET_MAX 256

/*
 * Greedy max-min subset in palette-opt metric space: each new index maximizes
 * the minimum squared distance to indices already chosen. Deterministic.
 *
 * out_subset[pen] = palette index for pen slot `pen` (0..n-1).
 * fix may pin individual pens; remaining pens are filled in ascending pen order.
 *
 * colors: 768 floats in pen/display metric space (hardware-quantized).
 */
int palette_subset_spread_colors_fix(const float *colors, int n, const PaletteSubsetFix *fix,
	unsigned char *out_subset);

/*
 * Same as palette_subset_spread_colors_fix (spread in pen_colors space); when
 * json_export is non-NULL, records one spread trace step per pen.
 * target_colors / dist_sq are used only for the trace cost evaluation.
 */
int palette_subset_spread_colors_fix_trace(const float *pen_colors, int n,
	const PaletteSubsetFix *fix, unsigned char *out_subset,
	struct PaletteOptJsonExport *json_export, const float *target_colors, const float *dist_sq,
	const double *alpha, float lambda);

int palette_subset_spread_colors(const float *colors, int n, unsigned char *out_subset);

int palette_subset_spread(const unsigned char *pal768, int n, unsigned char *out_subset);

#endif /* PALETTE_OPT_SUBSET_SPREAD_H */
