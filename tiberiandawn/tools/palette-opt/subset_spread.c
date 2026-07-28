/*
 * subset_spread.c - Farthest-point (max-min) subset selection in metric space.
 */

#include "subset_spread.h"
#include "json_export.h"
#include "palette_color.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float color_dist_sq(const float *a, const float *b)
{
	const float dy = a[0] - b[0];
	const float du = a[1] - b[1];
	const float dv = a[2] - b[2];
	return dy * dy + du * du + dv * dv;
}

static float min_dist_to_palette_list(const float *colors, int idx,
	const unsigned char *palette_list, int count)
{
	float min_d = FLT_MAX;
	int i;
	const float *p = &colors[3 * idx];
	for (i = 0; i < count; i++) {
		const float d = color_dist_sq(p, &colors[3 * (int)palette_list[i]]);
		if (d < min_d)
			min_d = d;
	}
	return min_d;
}

static int pick_farthest_from_centroid(const float *colors, const unsigned char *used)
{
	float centroid[3] = {0.0f, 0.0f, 0.0f};
	int best_i = 0;
	float best_d = -1.0f;
	int i;

	for (i = 0; i < 256; i++) {
		centroid[0] += colors[3 * i + 0];
		centroid[1] += colors[3 * i + 1];
		centroid[2] += colors[3 * i + 2];
	}
	centroid[0] /= 256.0f;
	centroid[1] /= 256.0f;
	centroid[2] /= 256.0f;

	for (i = 0; i < 256; i++) {
		const float d = color_dist_sq(&colors[3 * i], centroid);
		if (used[i])
			continue;
		if (d > best_d) {
			best_d = d;
			best_i = i;
		}
	}
	return best_i;
}

static int pick_farthest_to_set(const float *colors, const unsigned char *used,
	const unsigned char *palette_list, int palette_count)
{
	int best_i = -1;
	float best_min = -1.0f;
	int i;

	for (i = 0; i < 256; i++) {
		float min_d;
		if (used[i])
			continue;
		min_d = min_dist_to_palette_list(colors, i, palette_list, palette_count);
		if (min_d > best_min) {
			best_min = min_d;
			best_i = i;
		}
	}
	return best_i;
}

int palette_subset_spread_colors_fix(const float *colors, int n, const PaletteSubsetFix *fix,
	unsigned char *out_subset)
{
	unsigned char used[256];
	unsigned char palette_list[PALETTE_SUBSET_MAX];
	int free_pen[PALETTE_SUBSET_MAX];
	int palette_count = 0;
	int free_count = 0;
	int pen;
	int fi;

	if (!colors || !out_subset || n <= 0 || n > PALETTE_SUBSET_MAX)
		return 0;

	memset(used, 0, sizeof(used));
	memset(out_subset, 0, (size_t)n);

	if (fix) {
		for (pen = 0; pen < n; pen++) {
			if (!palette_subset_fix_is_fixed(fix, pen))
				continue;
			{
				const int pal_idx = fix->palette_index[pen];
				if (pal_idx < 0 || pal_idx > 255 || used[pal_idx]) {
					fprintf(stderr,
						"error: invalid or duplicate fixed palette index %d at pen %d\n",
						pal_idx, pen);
					return 0;
				}
				out_subset[pen] = (unsigned char)pal_idx;
				used[pal_idx] = 1;
				palette_list[palette_count++] = (unsigned char)pal_idx;
			}
		}
	}

	for (pen = 0; pen < n; pen++) {
		if (fix && palette_subset_fix_is_fixed(fix, pen))
			continue;
		free_pen[free_count++] = pen;
	}

	for (fi = 0; fi < free_count; fi++) {
		int pick;
		if (palette_count == 0) {
			pick = pick_farthest_from_centroid(colors, used);
		} else {
			pick = pick_farthest_to_set(colors, used, palette_list, palette_count);
		}
		if (pick < 0)
			return 0;
		out_subset[free_pen[fi]] = (unsigned char)pick;
		used[pick] = 1;
		palette_list[palette_count++] = (unsigned char)pick;
	}

	if (palette_count != n)
		return 0;

	return n;
}

static int spread_pick_for_pen(const float *colors, const unsigned char *used,
	const unsigned char *out_subset, int pen, const PaletteSubsetFix *fix, int pen_slot)
{
	int pick;

	if (fix && palette_subset_fix_is_fixed(fix, pen_slot)) {
		pick = fix->palette_index[pen_slot];
		if (pick < 0 || pick > 255 || used[pick])
			return -1;
		return pick;
	}

	if (pen == 0) {
		pick = pick_farthest_from_centroid(colors, used);
	} else {
		pick = pick_farthest_to_set(colors, used, out_subset, pen);
	}
	return pick;
}

int palette_subset_spread_colors_fix_trace(const float *pen_colors, int n,
	const PaletteSubsetFix *fix, unsigned char *out_subset,
	PaletteOptJsonExport *json_export, const float *target_colors, const float *dist_sq,
	const double *alpha, float lambda)
{
	unsigned char used[256];
	int pen;

	if (!pen_colors || !out_subset || n <= 0 || n > PALETTE_SUBSET_MAX)
		return 0;
	if (!json_export || !dist_sq || !target_colors)
		return palette_subset_spread_colors_fix(pen_colors, n, fix, out_subset);

	memset(used, 0, sizeof(used));
	memset(out_subset, 0, (size_t)n);

	for (pen = 0; pen < n; pen++) {
		const int pick = spread_pick_for_pen(pen_colors, used, out_subset, pen, fix, pen);
		if (pick < 0) {
			fprintf(stderr, "error: spread trace failed at pen %d\n", pen);
			return 0;
		}
		out_subset[pen] = (unsigned char)pick;
		used[pick] = 1;
		if (!palette_opt_json_spread_step(json_export, pen, out_subset, target_colors, pen_colors,
				dist_sq, alpha, lambda)) {
			fprintf(stderr, "error: json spread export failed at pen %d\n", pen);
			return 0;
		}
	}

	return n;
}

int palette_subset_spread_colors(const float *colors, int n, unsigned char *out_subset)
{
	return palette_subset_spread_colors_fix(colors, n, NULL, out_subset);
}

int palette_subset_spread(const unsigned char *pal768, int n, unsigned char *out_subset)
{
	float colors[768];
	if (!pal768)
		return 0;
	palette_build_opt_colors(pal768, colors);
	return palette_subset_spread_colors(colors, n, out_subset);
}
