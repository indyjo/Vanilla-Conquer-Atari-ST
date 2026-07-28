/*
 * subset_opt.h - Subset selection by simulated annealing on global weighted cost.
 */

#ifndef PALETTE_OPT_SUBSET_OPT_H
#define PALETTE_OPT_SUBSET_OPT_H

#include "subset_fix.h"
#include "weight_opt.h"

#include <stdio.h>

struct PaletteOptJsonExport;

#define PALETTE_OPT_NUM_COLORS 256

typedef struct PaletteSubsetOptParams {
	float lambda;
	int sa_max_iter;
	unsigned int sa_seed;
	float sa_t0;
	float sa_tmin;
	float sa_cool;
	int sa_log_every;
} PaletteSubsetOptParams;

typedef struct PaletteSubsetOptStats {
	double cost;
	double sum_e1;
	double sum_e2;
	int iterations;
	int accepted;
	int rejected;
} PaletteSubsetOptStats;

void palette_subset_opt_params_default(PaletteSubsetOptParams *p);

/*
 * Sparse histogram: lines "index count". Missing indices -> 0.
 * Empty path or missing file -> uniform alpha_i = 1/256. Returns 0 on success.
 */
int palette_hist_load(const char *path, double alpha[PALETTE_OPT_NUM_COLORS]);

void palette_hist_uniform(double alpha[PALETTE_OPT_NUM_COLORS]);

/* Sort subset[0..n-1] ascending by palette index (legacy; breaks pen-slot mapping). */
void palette_subset_sort(unsigned char *subset, int n);

/*
 * Evaluate C(S) = sum_i alpha_i * c(i). Optionally fill weights[256][16].
 * subset[pen] = palette index for pen `pen`. alpha may be NULL -> uniform.
 * target_colors = full-VGA metric; pen_colors = hardware-quantized metric.
 */
int palette_subset_evaluate(const float *target_colors, const float *pen_colors,
	const float *dist_sq, const unsigned char *subset, int subset_n, const double *alpha,
	float lambda, unsigned char (*weights)[PALETTE_OPT_WEIGHT_SLOTS], double *out_cost,
	double *out_sum_e1, double *out_sum_e2);

/*
 * Simulated annealing on subset[pen]. Fixed pens (fix) are not mutated.
 * Writes best subset back in pen order.
 */
int palette_subset_opt_anneal(const float *target_colors, const float *pen_colors,
	const float *dist_sq, unsigned char *subset_io, int subset_n, const PaletteSubsetFix *fix,
	const double *alpha, const PaletteSubsetOptParams *params, PaletteSubsetOptStats *stats,
	FILE *log, struct PaletteOptJsonExport *json_export);

#endif /* PALETTE_OPT_SUBSET_OPT_H */
