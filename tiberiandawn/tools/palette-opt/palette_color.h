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
	/* Hardware gun precision: quantize VGA 6-bit to this many bits (1..6; default 4 = STe). */
	int bits_per_channel;
} PaletteOptColorParams;

void palette_opt_color_params_default(PaletteOptColorParams *params);

/*
 * pal768: 256 × RGB, channels 0..63 (VGA 6-bit).
 * Each channel is quantized to bits_per_channel (rounded), normalized by
 * (2^bpc-1), then gamma-corrected; optionally transformed to YUV.
 * Quantize matches St_Pack_ST_HW_From_Rgb6_Channel: ((c6*max+31)/63).
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
