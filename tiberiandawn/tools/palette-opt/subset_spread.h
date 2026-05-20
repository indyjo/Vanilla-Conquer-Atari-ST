/*
 * subset_spread.h - Spread-maximizing palette subset (farthest-point sampling).
 */

#ifndef PALETTE_OPT_SUBSET_SPREAD_H
#define PALETTE_OPT_SUBSET_SPREAD_H

#define PALETTE_SUBSET_MAX 256

/*
 * Greedy max-min subset in palette-opt metric space: each new index maximizes
 * the minimum squared distance to indices already chosen. Deterministic.
 *
 * colors: 768 floats from palette_build_opt_colors() (2*y, u, v per entry).
 */
int palette_subset_spread_colors(const float *colors, int n, unsigned char *out_indices);

int palette_subset_spread(const unsigned char *pal768, int n, unsigned char *out_indices);

#endif /* PALETTE_OPT_SUBSET_SPREAD_H */
