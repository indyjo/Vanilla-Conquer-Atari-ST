#include "bmp.h"
#include "chart.h"
#include "hist.h"
#include "wsa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum {
	FILTER_ADD,
	FILTER_MUL,
	FILTER_DIV
} FilterType;

typedef struct {
	FilterType type;
	long long value;
} FilterOp;

#define MAX_FILTERS 64
#define MAX_PATHS 4096
#define FILTER_LINE_MAX 512

static int quiet;
static int chart_width = 48;
static int no_chart;
static int dense;
static const char *out_path;

static FilterOp filters[MAX_FILTERS];
static int n_filters;
static char filter_line[FILTER_LINE_MAX];

static const char *paths[MAX_PATHS];
static int n_paths;

static void usage(const char *prog)
{
	fprintf(stderr,
		"histtool - build or transform 256-index histograms for palette-opt\n"
		"\n"
		"Usage:\n"
		"  %s [OPTIONS] FILE [FILE ...]\n"
		"\n"
		"Inputs (positional, at least one):\n"
		"  *.bmp           8-bit indexed-color BMP; pixel indices accumulated\n"
		"  *.wsa           C&C WSA animation; all decoded frames accumulated\n"
		"  *.hist, *.txt   Sparse histogram: lines \"index count\" (# comments OK)\n"
		"  All inputs are summed into one histogram.\n"
		"\n"
		"Options:\n"
		"  -o, --output=FILE   Write histogram file (sparse by default)\n"
		"  --dense             Write all 256 \"index count\" lines\n"
		"  --add=N             Add integer N to every bin (repeatable)\n"
		"  --mul=N             Multiply every bin by integer N (N >= 1)\n"
		"  --div=N             Integer-divide every bin by N (N >= 1)\n"
		"  --chart-width=N     ASCII bar width (default 48)\n"
		"  --no-chart          Do not print chart to stdout\n"
		"  -q, --quiet         Less stderr logging\n"
		"  -h, --help          This help\n"
		"\n"
		"Filters run in command-line order after all inputs are summed.\n"
		"Counts are clamped to >= 0 after each filter step.\n"
		"\n"
		"BMP: Windows BMP v3, 8 bpp, BI_RGB or BI_RLE8.\n"
		"WSA: frame 0 LCW base image; later frames LCW + viewport XOR deltas.\n"
		"Output is compatible with palette-opt --hist FILE.\n"
		"\n"
		"Examples:\n"
		"  %s -o ui.hist assets/ui/*.bmp\n"
		"  %s -o map.hist EUROPE.WSA AFRICA.WSA\n"
		"  %s -o merged.hist stats_a.hist stats_b.hist frame.bmp\n"
		"  %s screen.bmp hud.bmp --add=1 --div=2 -o screen_trim.hist\n"
		"  %s counts.hist --mul=3 --no-chart -o counts_x3.hist\n",
		prog, prog, prog, prog, prog, prog);
}

static int path_has_ext(const char *path, const char *ext)
{
	size_t plen = strlen(path);
	size_t elen = strlen(ext);
	size_t i;

	if (plen < elen)
		return 0;
	for (i = 0; i < elen; i++) {
		char a = path[plen - elen + i];
		char b = ext[i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

static int path_is_directory(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	return S_ISDIR(st.st_mode);
}

static void filter_line_append(const char *token)
{
	size_t len = strlen(filter_line);
	if (len > 0 && len + 1 < sizeof(filter_line)) {
		strcat(filter_line, " ");
		len++;
	}
	if (len + strlen(token) < sizeof(filter_line))
		strcat(filter_line, token);
}

static int parse_filter_value(const char *s, long long *out)
{
	char *end = NULL;
	long long v;

	if (!s || !s[0])
		return -1;
	v = strtoll(s, &end, 10);
	if (end == s || (end && *end != '\0'))
		return -1;
	*out = v;
	return 0;
}

static int add_filter(FilterType type, long long value)
{
	char token[64];

	if (n_filters >= MAX_FILTERS) {
		fprintf(stderr, "error: too many filters\n");
		return -1;
	}
	if (type == FILTER_MUL || type == FILTER_DIV) {
		if (value < 1) {
			fprintf(stderr, "error: --%s=%lld (N must be >= 1)\n",
				type == FILTER_MUL ? "mul" : "div", (long long)value);
			return -1;
		}
	}

	filters[n_filters].type = type;
	filters[n_filters].value = value;
	n_filters++;

	if (type == FILTER_ADD)
		snprintf(token, sizeof(token), "add=%lld", (long long)value);
	else if (type == FILTER_MUL)
		snprintf(token, sizeof(token), "mul=%lld", (long long)value);
	else
		snprintf(token, sizeof(token), "div=%lld", (long long)value);
	filter_line_append(token);
	return 0;
}

static int parse_filter_arg(const char *arg, int *argi, int argc, char **argv)
{
	const char *val = NULL;
	long long n;
	FilterType type;

	if (!strncmp(arg, "--add=", 6)) {
		type = FILTER_ADD;
		val = arg + 6;
	} else if (!strcmp(arg, "--add")) {
		if (*argi + 1 >= argc) {
			fprintf(stderr, "error: %s requires a value\n", arg);
			return -1;
		}
		type = FILTER_ADD;
		val = argv[++(*argi)];
	} else if (!strncmp(arg, "--mul=", 6)) {
		type = FILTER_MUL;
		val = arg + 6;
	} else if (!strcmp(arg, "--mul")) {
		if (*argi + 1 >= argc) {
			fprintf(stderr, "error: %s requires a value\n", arg);
			return -1;
		}
		type = FILTER_MUL;
		val = argv[++(*argi)];
	} else if (!strncmp(arg, "--div=", 6)) {
		type = FILTER_DIV;
		val = arg + 6;
	} else if (!strcmp(arg, "--div")) {
		if (*argi + 1 >= argc) {
			fprintf(stderr, "error: %s requires a value\n", arg);
			return -1;
		}
		type = FILTER_DIV;
		val = argv[++(*argi)];
	} else {
		return 0;
	}

	if (parse_filter_value(val, &n) != 0) {
		fprintf(stderr, "error: invalid filter value: %s\n", val);
		return -1;
	}
	if (add_filter(type, n) != 0)
		return -1;
	return 1;
}

static int parse_option(const char *arg, int *argi, int argc, char **argv)
{
	if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
		usage(argv[0]);
		exit(0);
	}
	if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet")) {
		quiet = 1;
		return 1;
	}
	if (!strcmp(arg, "--dense")) {
		dense = 1;
		return 1;
	}
	if (!strcmp(arg, "--no-chart")) {
		no_chart = 1;
		return 1;
	}
	if (!strncmp(arg, "--chart-width=", 14)) {
		chart_width = atoi(arg + 14);
		return 1;
	}
	if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
		if (*argi + 1 >= argc) {
			fprintf(stderr, "error: %s requires a path\n", arg);
			exit(1);
		}
		out_path = argv[++(*argi)];
		return 1;
	}
	if (!strncmp(arg, "--output=", 9)) {
		out_path = arg + 9;
		return 1;
	}
	{
		int fr = parse_filter_arg(arg, argi, argc, argv);
		if (fr != 0) {
			if (fr < 0)
				exit(1);
			return 1;
		}
	}
	return 0;
}

static int input_kind(const char *path)
{
	if (path_has_ext(path, ".bmp"))
		return 1;
	if (path_has_ext(path, ".wsa"))
		return 2;
	if (path_has_ext(path, ".hist") || path_has_ext(path, ".txt"))
		return 3;
	return 0;
}

static void apply_filters(HistCounts *h)
{
	int i;
	for (i = 0; i < n_filters; i++) {
		switch (filters[i].type) {
		case FILTER_ADD:
			hist_filter_add(h, filters[i].value);
			break;
		case FILTER_MUL:
			hist_filter_mul(h, filters[i].value);
			break;
		case FILTER_DIV:
			hist_filter_div(h, filters[i].value);
			break;
		}
	}
}

int main(int argc, char **argv)
{
	HistCounts total;
	int n_bmp = 0;
	int n_wsa = 0;
	int n_hist = 0;
	int argi;
	int i;
	int rc = 0;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	hist_clear(&total);
	filter_line[0] = '\0';

	for (argi = 1; argi < argc; argi++) {
		const char *arg = argv[argi];

		if (arg[0] == '-' && parse_option(arg, &argi, argc, argv))
			continue;

		if (arg[0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", arg);
			usage(argv[0]);
			return 1;
		}

		if (n_paths >= MAX_PATHS) {
			fprintf(stderr, "error: too many input files\n");
			return 1;
		}
		paths[n_paths++] = arg;
	}

	if (n_paths == 0) {
		fprintf(stderr, "error: no input files\n");
		usage(argv[0]);
		return 1;
	}

	for (i = 0; i < n_paths; i++) {
		const char *path = paths[i];
		int kind;
		long long px;

		if (path_is_directory(path)) {
			fprintf(stderr, "error: %s is a directory (not supported)\n", path);
			return 1;
		}

		kind = input_kind(path);
		if (kind == 1) {
			px = bmp_hist_accumulate(path, &total);
			if (px < 0)
				return 1;
			n_bmp++;
			if (!quiet)
				fprintf(stderr, "histtool: %s  %lld pixels\n", path, px);
		} else if (kind == 2) {
			px = wsa_hist_accumulate(path, &total);
			if (px < 0)
				return 1;
			n_wsa++;
			if (!quiet)
				fprintf(stderr, "histtool: %s  %lld pixels\n", path, px);
		} else if (kind == 3) {
			if (hist_load_file(path, &total) != 0)
				return 1;
			n_hist++;
			if (!quiet)
				fprintf(stderr, "histtool: %s  loaded\n", path);
		} else {
			fprintf(stderr, "error: unknown type: %s (expected .bmp, .wsa, .hist, or .txt)\n", path);
			return 1;
		}
	}

	if (!quiet)
		fprintf(stderr, "histtool: total %lld pixels from %d bmp + %d wsa + %d hist\n",
			hist_total(&total), n_bmp, n_wsa, n_hist);

	if (n_filters > 0 && !quiet)
		fprintf(stderr, "histtool: filters: %s\n", filter_line);

	apply_filters(&total);

	if (out_path) {
		if (hist_save_file(out_path, &total, dense, n_bmp, n_wsa, n_hist, hist_total(&total),
				filter_line[0] ? filter_line : NULL) != 0)
			rc = 1;
	} else if (!quiet) {
		fprintf(stderr, "histtool: (no -o; histogram not written)\n");
	}

	if (!no_chart)
		chart_print_top64(&total, chart_width,
			filter_line[0] ? filter_line : NULL);

	return rc;
}
