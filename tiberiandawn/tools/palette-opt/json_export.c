/*
 * json_export.c - Streaming JSON trace writer for palette-opt / optviz.
 */

#include "json_export.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_EXPORT_NOTE_MAX 15
#define JSON_EXPORT_T0_FIELD_WIDTH 24

struct PaletteOptJsonExport {
	FILE *f;
	char *path;
	char *palette_path;
	unsigned char pal768[768];
	float colors[768];
	PaletteOptColorParams color_params;
	PaletteSubsetOptParams sa_params;
	float lambda;
	int export_every;
	int weight_granularity;
	int subset_n;
	double sa_t0;
	int sa_t0_set;
	long sa_t0_field_offset;

	int step_count;
	int closed;
	double best_cost;
	int iter_since_best;
};

static void json_write_json_string(FILE *f, const char *s)
{
	const unsigned char *p;

	fputc('"', f);
	if (!s) {
		fputc('"', f);
		return;
	}
	for (p = (const unsigned char *)s; *p; p++) {
		if (*p == '"' || *p == '\\')
			fputc('\\', f);
		fputc((int)*p, f);
	}
	fputc('"', f);
}

static void json_write_int_array(FILE *f, const int *v, int n)
{
	int i;

	fputc('[', f);
	for (i = 0; i < n; i++) {
		if (i)
			fputc(',', f);
		fprintf(f, "%d", v[i]);
	}
	fputc(']', f);
}

static void json_write_float_array(FILE *f, const float *v, int n)
{
	int i;

	fputc('[', f);
	for (i = 0; i < n; i++) {
		if (i)
			fputc(',', f);
		fprintf(f, "%.9g", (double)v[i]);
	}
	fputc(']', f);
}

static void json_write_subset(FILE *f, const unsigned char *subset, int n)
{
	int i;

	fputc('[', f);
	for (i = 0; i < n; i++) {
		if (i)
			fputc(',', f);
		fprintf(f, "%u", (unsigned)subset[i]);
	}
	fputc(']', f);
}

static void json_write_weights(FILE *f, const unsigned char (*weights)[16])
{
	int i;
	int k;

	fputc('[', f);
	for (i = 0; i < 256; i++) {
		if (i)
			fputc(',', f);
		fputc('[', f);
		for (k = 0; k < 16; k++) {
			if (k)
				fputc(',', f);
			fprintf(f, "%u", (unsigned)weights[i][k]);
		}
		fputc(']', f);
	}
	fputc(']', f);
}

static int json_write_header(PaletteOptJsonExport *exp)
{
	int pal_i[768];
	int i;

	if (!exp || !exp->f)
		return 0;

	for (i = 0; i < 768; i++)
		pal_i[i] = (int)exp->pal768[i];

	fprintf(exp->f, "{\n");
	fprintf(exp->f, "  \"version\": 1,\n");
	fprintf(exp->f, "  \"palette_path\": ");
	json_write_json_string(exp->f, exp->palette_path);
	fprintf(exp->f, ",\n");

	fprintf(exp->f, "  \"metric\": { ");
	if (exp->color_params.use_yuv) {
		fprintf(exp->f, "\"space\": \"yuv\", \"gamma\": %.9g, \"y_scale\": %.9g",
			(double)exp->color_params.gamma, (double)exp->color_params.y_scale);
	} else {
		fprintf(exp->f, "\"space\": \"rgb\", \"gamma\": %.9g", (double)exp->color_params.gamma);
	}
	fprintf(exp->f, " },\n");

	fprintf(exp->f, "  \"sa\": { \"lambda\": %.9g, \"max_iter\": %d, \"seed\": %u, "
		"\"cool\": %.9g, \"t0\": ",
		(double)exp->sa_params.lambda, exp->sa_params.sa_max_iter,
		(unsigned)exp->sa_params.sa_seed, (double)exp->sa_params.sa_cool);
	exp->sa_t0_field_offset = ftell(exp->f);
	fprintf(exp->f, "%-*s", JSON_EXPORT_T0_FIELD_WIDTH, "null");
	fprintf(exp->f, " },\n");

	fprintf(exp->f, "  \"export_every\": %d,\n", exp->export_every);
	fprintf(exp->f, "  \"weight_granularity\": %d,\n", exp->weight_granularity);
	fprintf(exp->f, "  \"subset_n\": %d,\n", exp->subset_n);

	fprintf(exp->f, "  \"pal768\": ");
	json_write_int_array(exp->f, pal_i, 768);
	fprintf(exp->f, ",\n");

	fprintf(exp->f, "  \"colors\": ");
	json_write_float_array(exp->f, exp->colors, 768);
	fprintf(exp->f, ",\n");

	fprintf(exp->f, "  \"steps\": [\n");
	fflush(exp->f);
	return 1;
}

static int json_patch_sa_t0(PaletteOptJsonExport *exp)
{
	char buf[JSON_EXPORT_T0_FIELD_WIDTH + 1];
	long resume;

	if (!exp || !exp->f || !exp->sa_t0_set || exp->sa_t0_field_offset <= 0)
		return 1;

	snprintf(buf, sizeof(buf), "%-*.*g", JSON_EXPORT_T0_FIELD_WIDTH,
		JSON_EXPORT_T0_FIELD_WIDTH - 1, exp->sa_t0);

	resume = ftell(exp->f);
	if (resume < 0)
		return 0;
	if (fseek(exp->f, exp->sa_t0_field_offset, SEEK_SET) != 0)
		return 0;
	if (fwrite(buf, 1, (size_t)JSON_EXPORT_T0_FIELD_WIDTH, exp->f)
		!= (size_t)JSON_EXPORT_T0_FIELD_WIDTH)
		return 0;
	if (fseek(exp->f, resume, SEEK_SET) != 0)
		return 0;
	return 1;
}

static int json_stream_step(PaletteOptJsonExport *exp, const char *phase, int iter,
	double T, int has_T, double cost, double e1, double e2, int move_in, int move_out,
	int has_move, const char *note, const unsigned char *subset, int subset_n,
	const unsigned char (*weights)[16])
{
	if (!exp || !exp->f || exp->closed || !phase || !subset || !weights)
		return 0;

	if (exp->step_count > 0)
		fprintf(exp->f, ",\n");

	fprintf(exp->f, "    {\n");
	fprintf(exp->f, "      \"phase\": ");
	json_write_json_string(exp->f, phase);
	fprintf(exp->f, ",\n");
	fprintf(exp->f, "      \"iter\": %d,\n", iter);
	if (has_T)
		fprintf(exp->f, "      \"T\": %.9g,\n", T);
	fprintf(exp->f, "      \"cost\": %.17g,\n", cost);
	fprintf(exp->f, "      \"best_cost\": %.17g,\n", exp->best_cost);
	fprintf(exp->f, "      \"iter_since_best\": %d,\n", exp->iter_since_best);
	fprintf(exp->f, "      \"e1\": %.17g,\n", e1);
	fprintf(exp->f, "      \"e2\": %.17g,\n", e2);
	fprintf(exp->f, "      \"subset\": ");
	json_write_subset(exp->f, subset, subset_n);
	fprintf(exp->f, ",\n");
	fprintf(exp->f, "      \"weights\": ");
	json_write_weights(exp->f, weights);
	if (has_move)
		fprintf(exp->f, ",\n      \"move\": { \"in\": %d, \"out\": %d }", move_in, move_out);
	if (note && note[0]) {
		fprintf(exp->f, ",\n      \"note\": ");
		json_write_json_string(exp->f, note);
	}
	fprintf(exp->f, "\n    }");

	exp->step_count++;
	fflush(exp->f);
	return 1;
}

static int json_record_step(PaletteOptJsonExport *exp, const char *phase, int iter,
	double T, int has_T, double cost, double e1, double e2, int move_in, int move_out,
	int has_move, const char *note, const unsigned char *subset, int subset_n,
	const unsigned char (*weights)[16])
{
	if (!exp)
		return 0;

	return json_stream_step(exp, phase, iter, T, has_T, cost, e1, e2, move_in, move_out,
		has_move, note, subset, subset_n, weights);
}

static int json_eval_record(PaletteOptJsonExport *exp, const char *phase, int iter,
	double T, int has_T, int move_in, int move_out, int has_move, const char *note,
	const unsigned char *subset, int subset_n, const float *colors, const float *dist_sq,
	const double *alpha, float lambda)
{
	unsigned char weights[256][16];
	double cost = 0.0;
	double e1 = 0.0;
	double e2 = 0.0;

	if (palette_subset_evaluate(colors, dist_sq, subset, subset_n, alpha, lambda, weights,
			&cost, &e1, &e2) != 0)
		return 0;

	if (cost < exp->best_cost)
		exp->best_cost = cost;

	return json_record_step(exp, phase, iter, T, has_T, cost, e1, e2, move_in, move_out,
		has_move, note, subset, subset_n, weights);
}

PaletteOptJsonExport *palette_opt_json_create(const char *path, const char *palette_path,
	const unsigned char *pal768, const float *colors,
	const PaletteOptColorParams *color_params, const PaletteSubsetOptParams *sa_params,
	float lambda, int export_every, int weight_granularity, int subset_n)
{
	PaletteOptJsonExport *exp;

	if (!path || !path[0] || !palette_path || !pal768 || !colors || !color_params
		|| !sa_params || export_every <= 0 || subset_n <= 0 || subset_n > 16)
		return NULL;

	exp = (PaletteOptJsonExport *)calloc(1, sizeof(*exp));
	if (!exp)
		return NULL;

	exp->path = strdup(path);
	exp->palette_path = strdup(palette_path);
	if (!exp->path || !exp->palette_path) {
		palette_opt_json_free(exp);
		return NULL;
	}

	exp->f = fopen(path, "w");
	if (!exp->f) {
		palette_opt_json_free(exp);
		return NULL;
	}
	setvbuf(exp->f, NULL, _IONBF, 0);

	memcpy(exp->pal768, pal768, 768);
	memcpy(exp->colors, colors, 768 * sizeof(float));
	exp->color_params = *color_params;
	exp->sa_params = *sa_params;
	exp->lambda = lambda;
	exp->export_every = export_every;
	exp->weight_granularity = weight_granularity > 0 ? weight_granularity : 1;
	exp->subset_n = subset_n;
	exp->best_cost = 1.0e300;
	exp->iter_since_best = 0;
	exp->sa_t0 = 0.0;
	exp->sa_t0_set = 0;
	exp->sa_t0_field_offset = 0;

	if (!json_write_header(exp)) {
		palette_opt_json_free(exp);
		return NULL;
	}

	return exp;
}

void palette_opt_json_close(PaletteOptJsonExport *exp)
{
	if (!exp || exp->closed)
		return;

	if (exp->f) {
		if (!json_patch_sa_t0(exp))
			fprintf(stderr, "warning: could not patch sa.t0 in %s\n",
				exp->path ? exp->path : "(json)");
		fprintf(exp->f, "\n  ]\n}\n");
		fflush(exp->f);
		if (fclose(exp->f) != 0)
			fprintf(stderr, "warning: error closing JSON export: %s\n",
				exp->path ? exp->path : "(json)");
		exp->f = NULL;
	}

	exp->closed = 1;
}

void palette_opt_json_free(PaletteOptJsonExport *exp)
{
	if (!exp)
		return;
	palette_opt_json_close(exp);
	free(exp->path);
	free(exp->palette_path);
	free(exp);
}

void palette_opt_json_set_sa_t0(PaletteOptJsonExport *exp, double t0)
{
	if (!exp)
		return;
	exp->sa_t0 = t0;
	exp->sa_t0_set = 1;
}

int palette_opt_json_should_export_anneal(int iter, int max_iter, int export_every)
{
	if (export_every <= 0)
		return 0;
	if (max_iter > 0 && iter > 0 && (iter % export_every) == 0)
		return 1;
	if (max_iter > 0 && iter == max_iter - 1)
		return 1;
	return 0;
}

int palette_opt_json_should_export_weights(int targets_done, int export_every)
{
	if (export_every <= 0)
		return 0;
	if (targets_done <= 0)
		return 1;
	if (targets_done >= 256)
		return 1;
	if ((targets_done % export_every) == 0)
		return 1;
	return 0;
}

int palette_opt_json_spread_step(PaletteOptJsonExport *exp, int pen,
	const unsigned char *subset, const float *colors, const float *dist_sq,
	const double *alpha, float lambda)
{
	char note[JSON_EXPORT_NOTE_MAX + 1];

	if (!exp || !subset || pen < 0 || pen >= exp->subset_n)
		return 0;

	snprintf(note, sizeof(note), "pen%d", pen);
	return json_eval_record(exp, "spread", pen, 0.0, 0, -1, -1, 0, note, subset, pen + 1,
		colors, dist_sq, alpha, lambda);
}

int palette_opt_json_anneal_step(PaletteOptJsonExport *exp, int iter, double T,
	double cost, double e1, double e2, int move_in, int move_out, const char *note,
	const unsigned char *subset, const float *colors, const float *dist_sq,
	const double *alpha, float lambda)
{
	unsigned char weights[256][16];

	if (!exp || !subset)
		return 0;

	if (palette_subset_evaluate(colors, dist_sq, subset, exp->subset_n, alpha, lambda, weights,
			NULL, NULL, NULL) != 0)
		return 0;

	if (cost < exp->best_cost) {
		exp->best_cost = cost;
		exp->iter_since_best = 0;
	} else if (iter > 0) {
		exp->iter_since_best++;
	}

	return json_record_step(exp, "anneal", iter, T, 1, cost, e1, e2, move_in, move_out,
		move_in >= 0 && move_out >= 0, note, subset, exp->subset_n, weights);
}

int palette_opt_json_weights_step(PaletteOptJsonExport *exp, int targets_done,
	const char *note, const unsigned char *subset, int subset_n,
	const unsigned char (*weights)[16], const float *colors, const float *dist_sq,
	const double *alpha, float lambda)
{
	double cost = 0.0;
	double e1 = 0.0;
	double e2 = 0.0;

	if (!exp || !subset || !weights)
		return 0;

	if (palette_subset_evaluate(colors, dist_sq, subset, subset_n, alpha, lambda,
			(unsigned char (*)[16])weights, &cost, &e1, &e2) != 0)
		return 0;

	if (cost < exp->best_cost)
		exp->best_cost = cost;

	return json_record_step(exp, "weights", targets_done, 0.0, 0, cost, e1, e2, -1, -1, 0,
		note, subset, subset_n, weights);
}

int palette_opt_json_export_every(const PaletteOptJsonExport *exp)
{
	if (!exp)
		return 0;
	return exp->export_every;
}

int palette_opt_json_step_count(const PaletteOptJsonExport *exp)
{
	if (!exp)
		return 0;
	return exp->step_count;
}
