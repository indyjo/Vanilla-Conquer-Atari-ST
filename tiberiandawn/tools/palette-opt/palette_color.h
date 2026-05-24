/*
 * palette_color.h - Gamma + scaled YUV used by palette-opt error metric.
 */

#ifndef PALETTE_OPT_COLOR_H
#define PALETTE_OPT_COLOR_H

#define PALETTE_OPT_PALETTE_SIZE 256
#define PALETTE_OPT_DIST_SQ_COUNT (PALETTE_OPT_PALETTE_SIZE * PALETTE_OPT_PALETTE_SIZE)

/*
 * pal768: 256 × RGB, channels 0..63 (VGA 6-bit).
 * colors: 768 floats written as (2*y, u, v) per index — same as find_best_dist().
 */
void palette_build_opt_colors(const unsigned char *pal768, float colors[768]);

/*
 * dist_sq: PALETTE_OPT_DIST_SQ_COUNT floats, row-major [i][j].
 * Symmetric with zero diagonal: dist_sq[i,j] == dist_sq[j,i], dist_sq[i,i] == 0.
 */
void palette_build_dist_sq_matrix(const float *colors, float dist_sq[PALETTE_OPT_DIST_SQ_COUNT]);

static inline float palette_dist_sq(const float *dist_sq, int a, int b)
{
	return dist_sq[a * PALETTE_OPT_PALETTE_SIZE + b];
}

#endif /* PALETTE_OPT_COLOR_H */
