#include "chart.h"

#include <stdio.h>

#define CHART_TOP_N 64

typedef struct {
	int idx;
	long long count;
} ChartEntry;

static void chart_sort_entries(ChartEntry *entries, int n)
{
	int i, j;
	for (i = 0; i < n - 1; i++) {
		for (j = i + 1; j < n; j++) {
			int swap = 0;
			if (entries[j].count > entries[i].count)
				swap = 1;
			else if (entries[j].count == entries[i].count && entries[j].idx < entries[i].idx)
				swap = 1;
			if (swap) {
				ChartEntry t = entries[i];
				entries[i] = entries[j];
				entries[j] = t;
			}
		}
	}
}

void chart_print_top64(const HistCounts *h, int bar_width, const char *filter_summary)
{
	ChartEntry entries[HIST_NUM_COLORS];
	int n = 0;
	int i;
	long long peak = 0;
	long long total;
	int nz;
	int peak_idx;
	int rows;
	int r;

	if (bar_width < 8)
		bar_width = 8;
	if (bar_width > 120)
		bar_width = 120;

	for (i = 0; i < HIST_NUM_COLORS; i++) {
		if (h->counts[i] > 0) {
			entries[n].idx = i;
			entries[n].count = h->counts[i];
			n++;
		}
	}

	chart_sort_entries(entries, n);
	rows = n < CHART_TOP_N ? n : CHART_TOP_N;

	for (i = 0; i < rows; i++) {
		if (entries[i].count > peak)
			peak = entries[i].count;
	}
	if (peak <= 0)
		peak = 1;

	printf(" idx | frequency");
	for (i = 13; i < 13 + bar_width; i++)
		(void)i;
	printf("%*s| count\n", bar_width - 10 > 0 ? bar_width - 10 : 1, "");
	printf("-----+");
	for (i = 0; i < bar_width; i++)
		putchar('-');
	printf("-+----------\n");

	for (r = 0; r < rows; r++) {
		int bars;
		int b;
		long long c = entries[r].count;
		bars = (int)((c * (long long)bar_width + peak - 1) / peak);
		if (bars > bar_width)
			bars = bar_width;
		printf("%4d |", entries[r].idx);
		for (b = 0; b < bar_width; b++)
			putchar(b < bars ? '#' : ' ');
		printf("|%10lld\n", c);
	}

	total = hist_total(h);
	nz = hist_nonzero(h);
	peak_idx = hist_peak_index(h, &peak);
	printf("\nsummary: %lld pixels | %d/256 non-zero | peak idx %d (%lld)",
		total, nz, peak_idx, peak);
	if (filter_summary && filter_summary[0])
		printf(" | filters: %s", filter_summary);
	printf("\n");
}
