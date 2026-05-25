/*
 * palette_color.c - Shared color space for palette-opt and subset selection.
 */

#include "palette_color.h"

#include <math.h>
#include <stddef.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static float dist_sq3(const float *a, const float *b)
{
	const float dx = a[0] - b[0];
	const float dy = a[1] - b[1];
	const float dz = a[2] - b[2];
	return dx * dx + dy * dy + dz * dz;
}

void palette_opt_color_params_default(PaletteOptColorParams *params)
{
	if (!params)
		return;
	params->gamma = 1.6f;
	params->y_scale = 2.0f;
	params->use_yuv = 1;
}

void palette_build_opt_colors(const unsigned char *pal768, float colors[768])
{
	palette_build_opt_colors_params(pal768, colors, NULL);
}

void palette_build_opt_colors_params(const unsigned char *pal768, float colors[768],
	const PaletteOptColorParams *params)
{
	int i;
	PaletteOptColorParams cfg;

	palette_opt_color_params_default(&cfg);
	if (params)
		cfg = *params;

	for (i = 0; i < 768; i++)
		colors[i] = powf((float)pal768[i] / 63.0f, cfg.gamma);

	if (!cfg.use_yuv)
		return;

	for (i = 0; i < 256; i++) {
		const float r = colors[3 * i + 0];
		const float g = colors[3 * i + 1];
		const float b = colors[3 * i + 2];
		const float y = 0.299f * r + 0.587f * g + 0.114f * b;
		const float u = 0.492f * (b - y);
		const float v = 0.877f * (r - y);
		colors[3 * i + 0] = cfg.y_scale * y;
		colors[3 * i + 1] = u;
		colors[3 * i + 2] = v;
	}
}

void palette_build_dist_sq_matrix(const float *colors, float dist_sq[PALETTE_OPT_DIST_SQ_COUNT])
{
	int i;

	if (!colors || !dist_sq)
		return;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
	for (i = 0; i < PALETTE_OPT_PALETTE_SIZE; i++) {
		const float *ci = &colors[3 * i];
		int j;

		dist_sq[i * PALETTE_OPT_PALETTE_SIZE + i] = 0.0f;
		for (j = i + 1; j < PALETTE_OPT_PALETTE_SIZE; j++) {
			const float d = dist_sq3(ci, &colors[3 * j]);
			dist_sq[i * PALETTE_OPT_PALETTE_SIZE + j] = d;
			dist_sq[j * PALETTE_OPT_PALETTE_SIZE + i] = d;
		}
	}
}
