/*
 * json_export.h - Optimization trace export for optviz animations.
 */

#ifndef PALETTE_OPT_JSON_EXPORT_H
#define PALETTE_OPT_JSON_EXPORT_H

#include "palette_color.h"
#include "subset_opt.h"

typedef struct PaletteOptJsonExport PaletteOptJsonExport;

/*
 * Open path and begin a streaming JSON trace (header + "steps": [).
 * Steps are appended to the file as optimization runs; call palette_opt_json_close
 * or palette_opt_json_free to finalize.
 */
PaletteOptJsonExport *palette_opt_json_create(const char *path, const char *palette_path,
	const unsigned char *pal768, const float *colors,
	const PaletteOptColorParams *color_params, const PaletteSubsetOptParams *sa_params,
	float lambda, int export_every, int weight_granularity, int subset_n);

void palette_opt_json_close(PaletteOptJsonExport *exp);
void palette_opt_json_free(PaletteOptJsonExport *exp);

/* Spread: pen 0..subset_n-1 just assigned in out_subset[0..pen]. */
int palette_opt_json_spread_step(PaletteOptJsonExport *exp, int pen,
	const unsigned char *subset, const float *colors, const float *dist_sq,
	const double *alpha, float lambda);

/*
 * Anneal grid export. move_in/move_out are -1 if not applicable (iter 0).
 * note may be NULL.
 */
int palette_opt_json_anneal_step(PaletteOptJsonExport *exp, int iter, double T,
	double cost, double e1, double e2, int move_in, int move_out, const char *note,
	const unsigned char *subset, const float *colors, const float *dist_sq,
	const double *alpha, float lambda);

/* Weights phase: targets_done is count of palette indices optimized (0..256). */
int palette_opt_json_weights_step(PaletteOptJsonExport *exp, int targets_done,
	const char *note, const unsigned char *subset, int subset_n,
	const unsigned char (*weights)[16], const float *colors, const float *dist_sq,
	const double *alpha, float lambda);

void palette_opt_json_set_sa_t0(PaletteOptJsonExport *exp, double t0);

int palette_opt_json_should_export_anneal(int iter, int max_iter, int export_every);

int palette_opt_json_should_export_weights(int targets_done, int export_every);

int palette_opt_json_export_every(const PaletteOptJsonExport *exp);

int palette_opt_json_step_count(const PaletteOptJsonExport *exp);

#endif /* PALETTE_OPT_JSON_EXPORT_H */
