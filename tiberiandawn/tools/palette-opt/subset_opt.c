/*
 * subset_opt.c - Global subset cost and simulated annealing.
 */

#include "subset_opt.h"
#include "json_export.h"
#include "subset_spread.h"
#include "weight_opt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PALETTE_OPT_DEFAULT_SUBSET_N 16

void palette_subset_opt_params_default(PaletteSubsetOptParams *p)
{
	if (!p)
		return;
	p->lambda = 0.3f;
	p->sa_max_iter = 100;
	p->sa_seed = 0;
	p->sa_t0 = 0.0f;
	p->sa_tmin = 1e-5f;
	p->sa_cool = 0.9995f;
	p->sa_log_every = 10;
}

void palette_hist_uniform(double alpha[PALETTE_OPT_NUM_COLORS])
{
	int i;
	const double u = 1.0 / (double)PALETTE_OPT_NUM_COLORS;
	for (i = 0; i < PALETTE_OPT_NUM_COLORS; i++)
		alpha[i] = u;
}

int palette_hist_load(const char *path, double alpha[PALETTE_OPT_NUM_COLORS])
{
	FILE *f;
	char line[256];
	double sum = 0.0;
	int i;

	palette_hist_uniform(alpha);
	if (!path || !path[0])
		return 0;

	f = fopen(path, "r");
	if (!f)
		return 0;

	while (fgets(line, (int)sizeof(line), f)) {
		int idx = -1;
		long long cnt = 0;
		char extra;

		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (sscanf(line, " %d %lld %c", &idx, &cnt, &extra) < 2)
			continue;
		if (idx < 0 || idx >= PALETTE_OPT_NUM_COLORS || cnt < 0)
			continue;
		alpha[idx] = (double)cnt;
		sum += (double)cnt;
	}
	fclose(f);

	if (sum <= 0.0) {
		palette_hist_uniform(alpha);
		return 0;
	}

	for (i = 0; i < PALETTE_OPT_NUM_COLORS; i++)
		alpha[i] /= sum;
	return 0;
}

void palette_subset_sort(unsigned char *subset, int n)
{
	int i, j;
	for (i = 0; i < n - 1; i++) {
		for (j = i + 1; j < n; j++) {
			if (subset[i] > subset[j]) {
				const unsigned char t = subset[i];
				subset[i] = subset[j];
				subset[j] = t;
			}
		}
	}
}

static int subset_has_index(const unsigned char *subset, int n, int idx)
{
	int i;
	for (i = 0; i < n; i++) {
		if ((int)subset[i] == idx)
			return 1;
	}
	return 0;
}

int palette_subset_evaluate(const float *colors, const float *dist_sq, const unsigned char *subset,
	int subset_n, const double *alpha_in, float lambda,
	unsigned char (*weights)[PALETTE_OPT_WEIGHT_SLOTS], double *out_cost, double *out_sum_e1,
	double *out_sum_e2)
{
	double alpha[PALETTE_OPT_NUM_COLORS];
	double cost = 0.0;
	double sum_e1 = 0.0;
	double sum_e2 = 0.0;
	int err = 0;
	int i;

	if (!colors || !dist_sq || !subset || subset_n <= 0)
		return -1;

	if (alpha_in)
		memcpy(alpha, alpha_in, sizeof(alpha));
	else
		palette_hist_uniform(alpha);

#ifdef _OPENMP
#pragma omp parallel for reduction(+ : cost, sum_e1, sum_e2) schedule(static)
#endif
	for (i = 0; i < PALETTE_OPT_NUM_COLORS; i++) {
		unsigned char wrow[PALETTE_OPT_WEIGHT_SLOTS];
		float e1, e2, blend;
		if (palette_weight_opt_best(colors, dist_sq, subset, subset_n, i, lambda, wrow) < 0.0f) {
			err = 1;
			continue;
		}
		palette_weight_e1_e2(colors, dist_sq, subset, subset_n, i, wrow, lambda, &e1, &e2,
			&blend);
		cost += alpha[i] * (double)blend;
		sum_e1 += alpha[i] * (double)e1;
		sum_e2 += alpha[i] * (double)e2;
		if (weights)
			memcpy(weights[i], wrow, PALETTE_OPT_WEIGHT_SLOTS);
	}

	if (err)
		return -1;

	if (out_cost)
		*out_cost = cost;
	if (out_sum_e1)
		*out_sum_e1 = sum_e1;
	if (out_sum_e2)
		*out_sum_e2 = sum_e2;
	return 0;
}

static unsigned int xorshift32(unsigned int *state)
{
	unsigned int x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static int pick_random_outside(const unsigned char *subset, int subset_n, unsigned int *rng)
{
	int v;
	int guard = 0;
	do {
		v = (int)(xorshift32(rng) % 256u);
		guard++;
	} while (subset_has_index(subset, subset_n, v) && guard < 512);
	return v;
}

static int pick_random_free_pen(const PaletteSubsetFix *fix, int subset_n, unsigned int *rng)
{
	int free_list[PALETTE_OPT_DEFAULT_SUBSET_N];
	int free_n = 0;
	int pen;

	for (pen = 0; pen < subset_n; pen++) {
		if (fix && palette_subset_fix_is_fixed(fix, pen))
			continue;
		free_list[free_n++] = pen;
	}
	if (free_n <= 0)
		return -1;
	return free_list[(int)(xorshift32(rng) % (unsigned)free_n)];
}

static void subset_enforce_fixes(const PaletteSubsetFix *fix, unsigned char *subset, int subset_n)
{
	int pen;
	if (!fix)
		return;
	for (pen = 0; pen < subset_n; pen++) {
		if (palette_subset_fix_is_fixed(fix, pen))
			subset[pen] = (unsigned char)fix->palette_index[pen];
	}
}

static void log_subset(FILE *log, const unsigned char *subset, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (i)
			fprintf(log, ",");
		fprintf(log, "%u", (unsigned)subset[i]);
	}
}

static void log_sa_table_rule(FILE *log, int log_every)
{
	fprintf(log,
		"  log: iterations 0-9, every %d (--sa-log-every), and each new-best\n", log_every);
}

static void log_sa_header(FILE *log)
{
	fprintf(log,
		"  %6s  %10s  %14s  %14s  %12s  %12s  %8s  %-10s\n",
		"iter", "T", "cost", "best", "e1", "e2", "move", "note");
}

static void log_sa_row(FILE *log, int iter, double T, double cost, double best, double e1,
	double e2, int in_idx, int out_idx, const char *note)
{
	fprintf(log, "  %6d  %10.4g  %14.8g  %14.8g  %12.6g  %12.6g  %3d->%-3d  %-10s\n",
		iter, T, cost, best, e1, e2, in_idx, out_idx, note);
}

static int log_sa_should_print(int iter, int log_every, const char *note)
{
	if (iter < 10)
		return 1;
	if (log_every > 0 && (iter % log_every) == 0)
		return 1;
	if (note && strcmp(note, "new-best") == 0)
		return 1;
	return 0;
}

int palette_subset_opt_anneal(const float *colors, const float *dist_sq, unsigned char *subset_io,
	int subset_n, const PaletteSubsetFix *fix, const double *alpha,
	const PaletteSubsetOptParams *params, PaletteSubsetOptStats *stats, FILE *log,
	PaletteOptJsonExport *json_export)
{
	PaletteSubsetOptParams defaults;
	unsigned char subset_best[PALETTE_OPT_DEFAULT_SUBSET_N];
	unsigned char subset_cur[PALETTE_OPT_DEFAULT_SUBSET_N];
	double cost_cur, cost_best, cost_try;
	double e1_cur, e2_cur, e1_best, e2_best;
	double e1_try, e2_try;
	double T;
	unsigned int rng;
	int iter;
	int last_iter = -1;
	int last_export_iter = -1;
	int accepted = 0;
	int rejected = 0;
	int slot;
	int out_idx;
	int in_idx;
	float lambda;
	int max_iter;
	int log_every;
	int export_every;
	float t0, tmin, cool;

	if (!colors || !dist_sq || !subset_io || subset_n <= 0
		|| subset_n > PALETTE_OPT_DEFAULT_SUBSET_N)
		return -1;

	if (!params) {
		palette_subset_opt_params_default(&defaults);
		params = &defaults;
	}
	if (!log)
		log = stderr;
	setvbuf(log, NULL, _IONBF, 0);

	lambda = params->lambda;
	max_iter = params->sa_max_iter >= 0 ? params->sa_max_iter : 100;
	log_every = params->sa_log_every > 0 ? params->sa_log_every : 10;
	export_every = json_export ? palette_opt_json_export_every(json_export) : 0;
	tmin = params->sa_tmin > 0.0f ? params->sa_tmin : 1e-5f;
	cool = params->sa_cool > 0.0f && params->sa_cool < 1.0f ? params->sa_cool : 0.9995f;
	rng = params->sa_seed ? params->sa_seed : (unsigned int)time(NULL);

	memcpy(subset_cur, subset_io, (size_t)subset_n);
	subset_enforce_fixes(fix, subset_cur, subset_n);

	fprintf(log, "subset-opt: lambda=%.3f  iter=%d  cool=%.5f  subset_n=%d\n",
		(double)lambda, max_iter, (double)cool, subset_n);
	if (fix && palette_subset_fix_count(fix) > 0)
		palette_subset_fix_log(fix, log);
#ifdef _OPENMP
	fprintf(log, "  OpenMP: enabled\n");
#endif
	fprintf(log, "  evaluating initial subset (256 weight passes)...\n");
	fflush(log);

	if (palette_subset_evaluate(colors, dist_sq, subset_cur, subset_n, alpha, lambda, NULL,
			&cost_cur, &e1_cur, &e2_cur) != 0)
		return -1;

	cost_best = cost_cur;
	e1_best = e1_cur;
	e2_best = e2_cur;
	memcpy(subset_best, subset_cur, (size_t)subset_n);

	t0 = params->sa_t0;
	if (t0 <= 0.0f) {
		/* Estimate scale from a few random swap deltas. */
		double avg_delta = 0.0;
		int samples = 0;
		int s;
		for (s = 0; s < 8; s++) {
			unsigned char trial[PALETTE_OPT_DEFAULT_SUBSET_N];
			double ct, e1t, e2t;
			memcpy(trial, subset_cur, (size_t)subset_n);
			slot = pick_random_free_pen(fix, subset_n, &rng);
			if (slot < 0)
				break;
			out_idx = pick_random_outside(trial, subset_n, &rng);
			trial[slot] = (unsigned char)out_idx;
			subset_enforce_fixes(fix, trial, subset_n);
			if (palette_subset_evaluate(colors, dist_sq, trial, subset_n, alpha, lambda, NULL,
					&ct, &e1t, &e2t) == 0) {
				const double d = fabs(ct - cost_cur);
				if (d > 0.0) {
					avg_delta += d;
					samples++;
				}
			}
		}
		t0 = samples > 0 ? (float)(avg_delta * 4.0) : (float)(cost_cur * 0.01);
		if (t0 < 1e-6f)
			t0 = 1e-3f;
	}

	T = (double)t0;

	if (json_export)
		palette_opt_json_set_sa_t0(json_export, T);

	fprintf(log, "  T0=%.6g\n", T);
	fprintf(log, "  init cost=%.8g  e1=%.8g  e2=%.8g  subset=[", cost_cur, e1_cur, e2_cur);
	log_subset(log, subset_cur, subset_n);
	fprintf(log, "]\n");
	log_sa_table_rule(log, log_every);
	log_sa_header(log);

	if (json_export) {
		if (!palette_opt_json_anneal_step(json_export, 0, T, cost_cur, e1_cur, e2_cur, -1, -1,
				"init", subset_cur, colors, dist_sq, alpha, lambda))
			return -1;
		last_export_iter = 0;
	}

	for (iter = 0; iter < max_iter && T > (double)tmin; iter++) {
		unsigned char trial[PALETTE_OPT_DEFAULT_SUBSET_N];
		double delta;
		int accept;
		const char *note = "";

		memcpy(trial, subset_cur, (size_t)subset_n);
		slot = pick_random_free_pen(fix, subset_n, &rng);
		if (slot < 0)
			break;
		in_idx = (int)trial[slot];
		out_idx = pick_random_outside(trial, subset_n, &rng);
		trial[slot] = (unsigned char)out_idx;
		subset_enforce_fixes(fix, trial, subset_n);

		if (palette_subset_evaluate(colors, dist_sq, trial, subset_n, alpha, lambda, NULL,
				&cost_try, &e1_try, &e2_try) != 0)
			continue;

		delta = cost_try - cost_cur;
		accept = 0;
		if (delta <= 0.0) {
			accept = 1;
			note = delta < 0.0 ? "improve" : "equal";
		} else {
			const double p = exp(-delta / T);
			const double r = (double)xorshift32(&rng) / 4294967295.0;
			if (r < p) {
				accept = 1;
				note = "anneal";
			} else {
				note = "reject";
			}
		}

		if (accept) {
			cost_cur = cost_try;
			e1_cur = e1_try;
			e2_cur = e2_try;
			memcpy(subset_cur, trial, (size_t)subset_n);
			accepted++;
			if (cost_cur < cost_best) {
				cost_best = cost_cur;
				e1_best = e1_cur;
				e2_best = e2_cur;
				memcpy(subset_best, subset_cur, (size_t)subset_n);
				note = "new-best";
			}
		} else {
			rejected++;
		}

		if (log_sa_should_print(iter, log_every, note)) {
			log_sa_row(log, iter, T, cost_cur, cost_best, e1_cur, e2_cur, in_idx, out_idx, note);
		}

		if (json_export && palette_opt_json_should_export_anneal(iter, max_iter, export_every)) {
			if (!palette_opt_json_anneal_step(json_export, iter, T, cost_cur, e1_cur, e2_cur,
					in_idx, out_idx, note, subset_cur, colors, dist_sq, alpha, lambda))
				return -1;
			last_export_iter = iter;
		}

		last_iter = iter;
		T *= (double)cool;
	}

	if (json_export && last_iter >= 0 && last_export_iter != last_iter) {
		if (!palette_opt_json_anneal_step(json_export, last_iter, T, cost_cur, e1_cur, e2_cur,
				in_idx, out_idx, "final", subset_cur, colors, dist_sq, alpha, lambda))
			return -1;
	}

	memcpy(subset_io, subset_best, (size_t)subset_n);
	subset_enforce_fixes(fix, subset_io, subset_n);

	fprintf(log, "subset-opt done: iter=%d  accepted=%d  rejected=%d\n", max_iter, accepted,
		rejected);
	fprintf(log, "  final cost=%.8g  e1=%.8g  e2=%.8g  subset=[", cost_best, e1_best, e2_best);
	log_subset(log, subset_best, subset_n);
	fprintf(log, "]\n");

	if (stats) {
		stats->cost = cost_best;
		stats->sum_e1 = e1_best;
		stats->sum_e2 = e2_best;
		stats->iterations = max_iter;
		stats->accepted = accepted;
		stats->rejected = rejected;
	}

	return 0;
}
