/*
 * weight_opt.c - Inner optimizer for Bayer dither weights.
 *
 * e1: sum_k (w_k/16) * ||t - c_k||^2  (mean per-phase error; equivalent to simulating
 *     16 Bayer cells when ranks 0..15 appear once and sum w_k = 16).
 * e2: ||t - sum_k (w_k/16) * c_k||^2
 *
 * palette_opt_weight_granularity restricts each w_k to multiples of G (G divides 16).
 * G=4 matches a 2x2 Bayer tile (four unit slots); G=1 is full 4x4 resolution.
 */

#include "weight_opt.h"

#include "palette_color.h"

#include <math.h>
#include <string.h>

int palette_opt_weight_granularity = 1;

static float dist_sq3(const float *a, const float *b)
{
	const float dx = a[0] - b[0];
	const float dy = a[1] - b[1];
	const float dz = a[2] - b[2];
	return dx * dx + dy * dy + dz * dz;
}

static float e1_cost_from_weights(int subset_n, const float pen_d_sq[16],
	const unsigned char *weights)
{
	const float inv = 1.0f / (float)PALETTE_OPT_WEIGHT_SUM;
	float e1 = 0.0f;
	int k;

	for (k = 0; k < subset_n; k++) {
		if (weights[k] == 0)
			continue;
		e1 += (float)weights[k] * inv * pen_d_sq[k];
	}
	return e1;
}

static float e2_cost_from_weights(const float *target, int subset_n,
	const float pen_colors[16][3], const unsigned char *weights)
{
	float mix[3] = {0.0f, 0.0f, 0.0f};
	int k;

	for (k = 0; k < subset_n; k++) {
		const float w = (float)weights[k];
		mix[0] += w * pen_colors[k][0];
		mix[1] += w * pen_colors[k][1];
		mix[2] += w * pen_colors[k][2];
	}
	{
		const float inv = 1.0f / (float)PALETTE_OPT_WEIGHT_SUM;
		const float m[3] = {mix[0] * inv, mix[1] * inv, mix[2] * inv};
		return dist_sq3(target, m);
	}
}

void palette_weight_e1_e2(const float *colors, const float *dist_sq, const unsigned char *subset,
	int subset_n, int target_i, const unsigned char *weights, float lambda, float *out_e1,
	float *out_e2, float *out_blend)
{
	const float *target = &colors[3 * target_i];
	float pen_colors[16][3];
	float pen_d_sq[16];
	float e1, e2;
	int k;

	for (k = 0; k < subset_n; k++) {
		const int pal = (int)subset[k];
		const float *pen = &colors[3 * pal];
		pen_colors[k][0] = pen[0];
		pen_colors[k][1] = pen[1];
		pen_colors[k][2] = pen[2];
		pen_d_sq[k] = palette_dist_sq(dist_sq, target_i, pal);
	}

	e1 = e1_cost_from_weights(subset_n, pen_d_sq, weights);
	e2 = e2_cost_from_weights(target, subset_n, pen_colors, weights);

	if (out_e1)
		*out_e1 = e1;
	if (out_e2)
		*out_e2 = e2;
	if (out_blend)
		*out_blend = (1.0f - lambda) * e1 + lambda * e2;
}

typedef struct WeightSearchCtx {
	const float *target;
	float lambda;
	int subset_n;
	float pen_colors[16][3];
	float pen_d_sq[16];
	unsigned char weights[PALETTE_OPT_WEIGHT_SLOTS];
	float best_cost;
} WeightSearchCtx;

static float blend_cost_from_weights(const WeightSearchCtx *ctx, const unsigned char *weights)
{
	const float e1 = e1_cost_from_weights(ctx->subset_n, ctx->pen_d_sq, weights);
	const float e2 = e2_cost_from_weights(ctx->target, ctx->subset_n, ctx->pen_colors, weights);
	return (1.0f - ctx->lambda) * e1 + ctx->lambda * e2;
}

static void try_weights(WeightSearchCtx *ctx, const unsigned char *weights)
{
	const float cost = blend_cost_from_weights(ctx, weights);
	if (cost >= 0.0f && (ctx->best_cost < 0.0f || cost < ctx->best_cost)) {
		ctx->best_cost = cost;
		memcpy(ctx->weights, weights, (size_t)ctx->subset_n);
	}
}

/*
 * Distribute `left` into `k` positive parts (each a multiple of gran); write parts[0..k-1].
 */
static void partition_positive(int k, int left, int gran, int *parts, int slot, WeightSearchCtx *ctx,
	const int *pen_slots, int num_pens)
{
	int i;
	unsigned char weights[PALETTE_OPT_WEIGHT_SLOTS];
	const int units = left / gran;

	if (gran <= 0 || left % gran != 0 || units < k)
		return;

	if (k == 1) {
		parts[slot] = left;
		memset(weights, 0, sizeof(weights));
		for (i = 0; i < num_pens; i++)
			weights[pen_slots[i]] = (unsigned char)parts[i];
		try_weights(ctx, weights);
		return;
	}

	for (i = 1; i <= units - (k - 1); i++) {
		parts[slot] = i * gran;
		partition_positive(k - 1, left - i * gran, gran, parts, slot + 1, ctx, pen_slots,
			num_pens);
	}
}

/* Choose `num_pens` distinct slots among subset_n, then positive partitions of 16. */
static void choose_pens(WeightSearchCtx *ctx, int num_pens, int start, int slot,
	int *pen_slots)
{
	int i;
	int parts[4];

	if (slot >= num_pens) {
		partition_positive(num_pens, PALETTE_OPT_WEIGHT_SUM, palette_opt_weight_granularity,
			parts, 0, ctx, pen_slots, num_pens);
		return;
	}

	for (i = start; i <= ctx->subset_n - (num_pens - slot); i++) {
		pen_slots[slot] = i;
		choose_pens(ctx, num_pens, i + 1, slot + 1, pen_slots);
	}
}

static float weight_search_solve(WeightSearchCtx *ctx)
{
	int pen_slots[4];
	int num_pens;

	ctx->best_cost = -1.0f;
	memset(ctx->weights, 0, sizeof(ctx->weights));

	for (num_pens = 1; num_pens <= PALETTE_OPT_MAX_NONZERO_WEIGHTS; num_pens++)
		choose_pens(ctx, num_pens, 0, 0, pen_slots);

	return ctx->best_cost;
}

float palette_weight_opt_best(const float *colors, const float *dist_sq, const unsigned char *subset,
	int subset_n, int target_i, float lambda, unsigned char *out_weights)
{
	WeightSearchCtx ctx;
	int pen_idx;
	const int row = target_i * PALETTE_OPT_PALETTE_SIZE;

	if (!colors || !dist_sq || !subset || !out_weights || subset_n <= 0
		|| subset_n > PALETTE_OPT_WEIGHT_SLOTS || target_i < 0
		|| target_i >= PALETTE_OPT_PALETTE_SIZE)
		return -1.0f;

	memset(&ctx, 0, sizeof(ctx));
	ctx.target = &colors[3 * target_i];
	ctx.lambda = lambda;
	ctx.subset_n = subset_n;
	for (pen_idx = 0; pen_idx < subset_n; pen_idx++) {
		const int pal = (int)subset[pen_idx];
		const float *p = &colors[3 * pal];
		ctx.pen_colors[pen_idx][0] = p[0];
		ctx.pen_colors[pen_idx][1] = p[1];
		ctx.pen_colors[pen_idx][2] = p[2];
		ctx.pen_d_sq[pen_idx] = dist_sq[row + pal];
	}

	if (weight_search_solve(&ctx) < 0.0f)
		return -1.0f;

	memset(out_weights, 0, PALETTE_OPT_WEIGHT_SLOTS);
	memcpy(out_weights, ctx.weights, (size_t)subset_n);
	return ctx.best_cost;
}
