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
	/*
	 * Hardware gun precision for *pens* only (1..6; default 4 = STe).
	 * VGA targets always keep full 6-bit precision.
	 */
	int bits_per_channel;
} PaletteOptColorParams;

void palette_opt_color_params_default(PaletteOptColorParams *params);

/*
 * Target metric space: full VGA 6-bit (bits 6–7 masked), normalized by 63,
 * gamma-corrected; optionally YUV. Does not apply bits_per_channel.
 */
void palette_build_opt_colors(const unsigned char *pal768, float colors[768]);
void palette_build_opt_colors_params(const unsigned char *pal768, float colors[768],
	const PaletteOptColorParams *params);

/*
 * Pen/display metric space: VGA 6-bit quantized to bits_per_channel
 * (St_Pack_ST_HW_From_Rgb6_Channel rounding: (c6*max+31)/63), then same
 * gamma / YUV as targets. Used when an index is a hardware pen.
 */
void palette_build_opt_pen_colors_params(const unsigned char *pal768, float pen_colors[768],
	const PaletteOptColorParams *params);

/*
 * dist_sq[i,j] = ||target_colors[i] - pen_colors[j]||^2 (asymmetric).
 * Diagonal of ||t[i]-p[i]|| may be nonzero when bpc < 6.
 */
void palette_build_dist_sq_matrix(const float *target_colors, const float *pen_colors,
	float dist_sq[PALETTE_OPT_DIST_SQ_COUNT]);

static inline float palette_dist_sq(const float *dist_sq, int target_i, int pen_i)
{
	return dist_sq[target_i * PALETTE_OPT_PALETTE_SIZE + pen_i];
}

#endif /* PALETTE_OPT_COLOR_H */
