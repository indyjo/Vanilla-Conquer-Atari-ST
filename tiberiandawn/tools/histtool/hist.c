#include "hist.h"

#include <stdio.h>
#include <string.h>

void hist_clear(HistCounts *h)
{
	memset(h, 0, sizeof(*h));
}

void hist_add(HistCounts *dst, const HistCounts *src)
{
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++)
		dst->counts[i] += src->counts[i];
}

int hist_load_file(const char *path, HistCounts *out)
{
	FILE *f;
	char line[256];

	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return -1;
	}

	while (fgets(line, (int)sizeof(line), f)) {
		int idx = -1;
		long long cnt = 0;
		char extra;

		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (sscanf(line, " %d %lld %c", &idx, &cnt, &extra) < 2)
			continue;
		if (idx < 0 || idx >= HIST_NUM_COLORS || cnt < 0)
			continue;
		out->counts[idx] += cnt;
	}
	fclose(f);
	return 0;
}

static void hist_clamp_nonneg(HistCounts *h)
{
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++) {
		if (h->counts[i] < 0)
			h->counts[i] = 0;
	}
}

void hist_filter_add(HistCounts *h, long long n)
{
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++)
		h->counts[i] += n;
	hist_clamp_nonneg(h);
}

void hist_filter_mul(HistCounts *h, long long n)
{
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++)
		h->counts[i] *= n;
	hist_clamp_nonneg(h);
}

void hist_filter_div(HistCounts *h, long long n)
{
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++)
		h->counts[i] /= n;
	hist_clamp_nonneg(h);
}

long long hist_total(const HistCounts *h)
{
	long long sum = 0;
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++)
		sum += h->counts[i];
	return sum;
}

int hist_nonzero(const HistCounts *h)
{
	int n = 0;
	int i;
	for (i = 0; i < HIST_NUM_COLORS; i++) {
		if (h->counts[i] > 0)
			n++;
	}
	return n;
}

int hist_peak_index(const HistCounts *h, long long *out_peak)
{
	int best = 0;
	int i;
	long long peak = 0;

	for (i = 0; i < HIST_NUM_COLORS; i++) {
		if (h->counts[i] > peak) {
			peak = h->counts[i];
			best = i;
		}
	}
	if (out_peak)
		*out_peak = peak;
	return best;
}

int hist_save_file(const char *path, const HistCounts *h, int dense, int n_bmp, int n_wsa, int n_hist,
	long long total_pixels, const char *filter_line)
{
	FILE *f;
	int i;
	int lines = 0;

	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "error: cannot write %s\n", path);
		return -1;
	}

	fprintf(f, "# histtool: %d bmp, %d wsa, %d hist, %lld pixels\n", n_bmp, n_wsa, n_hist, total_pixels);
	if (filter_line && filter_line[0])
		fprintf(f, "# filters: %s\n", filter_line);

	for (i = 0; i < HIST_NUM_COLORS; i++) {
		if (dense || h->counts[i] > 0) {
			fprintf(f, "%d %lld\n", i, h->counts[i]);
			lines++;
		}
	}
	fclose(f);

	if (!dense) {
		lines = hist_nonzero(h);
	}

	fprintf(stderr, "histtool: wrote %s (%d lines, %s)\n", path, lines, dense ? "dense" : "sparse");
	return 0;
}
