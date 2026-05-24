#ifndef HISTTOOL_HIST_H
#define HISTTOOL_HIST_H

#define HIST_NUM_COLORS 256

typedef struct {
	long long counts[HIST_NUM_COLORS];
} HistCounts;

void hist_clear(HistCounts *h);
void hist_add(HistCounts *dst, const HistCounts *src);

/* Load sparse "index count" file; adds into *out (does not clear). Returns 0 on success. */
int hist_load_file(const char *path, HistCounts *out);

/* Write histogram; dense=0 omits zero bins. filter_line may be NULL. */
int hist_save_file(const char *path, const HistCounts *h, int dense, int n_bmp, int n_wsa, int n_hist,
	long long total_pixels, const char *filter_line);

void hist_filter_add(HistCounts *h, long long n);
void hist_filter_mul(HistCounts *h, long long n);
void hist_filter_div(HistCounts *h, long long n);

long long hist_total(const HistCounts *h);
int hist_nonzero(const HistCounts *h);
int hist_peak_index(const HistCounts *h, long long *out_peak);

#endif
