/*
 * palette_color.h - Palette color transform for palette-opt metric space.
 */

#ifndef PALETTE_OPT_COLOR_H
#define PALETTE_OPT_COLOR_H

#define PALETTE_OPT_PALETTE_SIZE 256
#define PALETTE_OPT_DIST_SQ_COUNT (PALETTE_OPT_PALETTE_SIZE * PALETTE_OPT_PALETTE_SIZE)

typedef struct PaletteOptColorParams {
	float gamma;
	float y_scale;
	int use_yuv;
} PaletteOptColorParams;

void palette_opt_color_params_default(PaletteOptColorParams *params);

/*
 * pal768: 256 × RGB, channels 0..63 (VGA 6-bit).
 * colors: 768 floats written as either gamma-corrected RGB or transformed YUV.
 * With default params this matches the historical (2*y, u, v) metric.
 */
void palette_build_opt_colors(const unsigned char *pal768, float colors[768]);
void palette_build_opt_colors_params(const unsigned char *pal768, float colors[768],
	const PaletteOptColorParams *params);

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
