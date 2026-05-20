/*
 * subset_spread.c - Farthest-point (max-min) subset selection in metric YUV space.
 */

#include "subset_spread.h"
#include "palette_color.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

static float color_dist_sq(const float *a, const float *b)
{
	const float dy = a[0] - b[0];
	const float du = a[1] - b[1];
	const float dv = a[2] - b[2];
	return dy * dy + du * du + dv * dv;
}

static int index_in_set(const unsigned char *set, int count, int idx)
{
	int i;
	for (i = 0; i < count; i++) {
		if ((int)set[i] == idx)
			return 1;
	}
	return 0;
}

static float min_dist_to_set(const float *colors, int idx, const unsigned char *set, int count)
{
	float min_d = FLT_MAX;
	int i;
	const float *p = &colors[3 * idx];
	for (i = 0; i < count; i++) {
		const float d = color_dist_sq(p, &colors[3 * (int)set[i]]);
		if (d < min_d)
			min_d = d;
	}
	return min_d;
}

static int cmp_uchar(const void *a, const void *b)
{
	return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

int palette_subset_spread_colors(const float *colors, int n, unsigned char *out_indices)
{
	unsigned char selected[PALETTE_SUBSET_MAX];
	float centroid[3] = {0.0f, 0.0f, 0.0f};
	int i;
	int k;

	if (!colors || !out_indices || n <= 0 || n > PALETTE_SUBSET_MAX)
		return 0;

	for (i = 0; i < 256; i++) {
		centroid[0] += colors[3 * i + 0];
		centroid[1] += colors[3 * i + 1];
		centroid[2] += colors[3 * i + 2];
	}
	centroid[0] /= 256.0f;
	centroid[1] /= 256.0f;
	centroid[2] /= 256.0f;

	/* Seed: palette entry furthest from the mean color. */
	{
		int best_i = 0;
		float best_d = -1.0f;
		for (i = 0; i < 256; i++) {
			const float d = color_dist_sq(&colors[3 * i], centroid);
			if (d > best_d) {
				best_d = d;
				best_i = i;
			}
		}
		selected[0] = (unsigned char)best_i;
	}

	for (k = 1; k < n; k++) {
		int best_i = -1;
		float best_min = -1.0f;
		for (i = 0; i < 256; i++) {
			float min_d;
			if (index_in_set(selected, k, i))
				continue;
			min_d = min_dist_to_set(colors, i, selected, k);
			if (min_d > best_min) {
				best_min = min_d;
				best_i = i;
			}
		}
		if (best_i < 0)
			break;
		selected[k] = (unsigned char)best_i;
	}

	if (k < n)
		return 0;

	memcpy(out_indices, selected, (size_t)n);
	qsort(out_indices, (size_t)n, 1, cmp_uchar);
	return n;
}

int palette_subset_spread(const unsigned char *pal768, int n, unsigned char *out_indices)
{
	float colors[768];
	if (!pal768)
		return 0;
	palette_build_opt_colors(pal768, colors);
	return palette_subset_spread_colors(colors, n, out_indices);
}
