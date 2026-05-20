/*
 * palette_color.c - Shared color space for palette-opt and subset selection.
 */

#include "palette_color.h"

#include <math.h>

void palette_build_opt_colors(const unsigned char *pal768, float colors[768])
{
	int i;

	for (i = 0; i < 768; i++)
		colors[i] = powf((float)pal768[i] / 63.0f, 1.6f);

	for (i = 0; i < 256; i++) {
		const float r = colors[3 * i + 0];
		const float g = colors[3 * i + 1];
		const float b = colors[3 * i + 2];
		const float y = 0.299f * r + 0.587f * g + 0.114f * b;
		const float u = 0.492f * (b - y);
		const float v = 0.877f * (r - y);
		colors[3 * i + 0] = 2.0f * y;
		colors[3 * i + 1] = u;
		colors[3 * i + 2] = v;
	}
}
