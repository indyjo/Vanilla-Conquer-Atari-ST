#include "palette_color.h"
#include "ply_dump.h"
#include "subset_fix.h"
#include "subset_opt.h"
#include "subset_spread.h"
#include "weight_opt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

extern unsigned char PLAYPAL[];

float colors[768];
static float dist_sq[PALETTE_OPT_DIST_SQ_COUNT];
static unsigned char raw_pal[768];

static unsigned char subset[PALETTE_SUBSET_MAX];
static int subset_count = 16;

#define W16_MAGIC "W16"
#define W16_FILE_BYTES (4 + 16 + 256 * 16)

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

static int load_palette_file(const char *path, unsigned char *dst768)
{
	FILE *f;
	long size;
	unsigned char hdr[14];
	uint16_t total_frames;
	uint16_t flags;
	long pal_off;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open palette source: %s\n", path);
		return 0;
	}

	if (fseek(f, 0L, SEEK_END) != 0) {
		fclose(f);
		fprintf(stderr, "error: failed to seek in %s\n", path);
		return 0;
	}
	size = ftell(f);
	if (size < 0) {
		fclose(f);
		fprintf(stderr, "error: failed to query size of %s\n", path);
		return 0;
	}
	if (fseek(f, 0L, SEEK_SET) != 0) {
		fclose(f);
		fprintf(stderr, "error: failed to rewind %s\n", path);
		return 0;
	}

	if (size == 768) {
		if (fread(dst768, 1, 768, f) != 768) {
			fclose(f);
			fprintf(stderr, "error: short read for PAL file %s\n", path);
			return 0;
		}
		fclose(f);
		return 1;
	}

	if (size < 14 + 768) {
		fclose(f);
		fprintf(stderr, "error: unsupported palette source size for %s\n", path);
		return 0;
	}
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fclose(f);
		fprintf(stderr, "error: failed reading WSA header from %s\n", path);
		return 0;
	}
	total_frames = read_le16(hdr + 0);
	flags = read_le16(hdr + 12);
	if ((flags & 1u) == 0u) {
		fclose(f);
		fprintf(stderr, "error: WSA has no embedded palette: %s\n", path);
		return 0;
	}
	pal_off = 14L + (long)(total_frames + 2) * 4L;
	if (pal_off < 0 || pal_off + 768L > size) {
		fclose(f);
		fprintf(stderr, "error: invalid palette offset in WSA: %s\n", path);
		return 0;
	}
	if (fseek(f, pal_off, SEEK_SET) != 0) {
		fclose(f);
		fprintf(stderr, "error: failed to seek WSA palette in %s\n", path);
		return 0;
	}
	if (fread(dst768, 1, 768, f) != 768) {
		fclose(f);
		fprintf(stderr, "error: short read for WSA palette in %s\n", path);
		return 0;
	}
	fclose(f);
	return 1;
}

static void print_help(FILE *out, const char *prog)
{
	fprintf(out,
		"palette-opt - dither weight tables from a 256-color palette\n"
		"\n"
		"Usage:\n"
		"  %s [options]\n"
		"\n"
		"Options:\n"
		"  -h, --help    Show this help and exit.\n"
		"  -o, --output FILE\n"
		"                Write C2P .W16 bundle (16-byte subset + 256x16 weights).\n"
		"                If FILE exists, exit with error unless -c/--continue.\n"
		"  -c, --continue\n"
		"                With -o: load subset from existing FILE, then overwrite.\n"
		"                Incompatible with --subset-from and --subset-init.\n"
		"  -p, --palette FILE\n"
		"                768-byte .PAL or .WSA with embedded palette.\n"
		"                Default: built-in TEMPERAT.PAL (playpal.c).\n"
		"  --dump-ply PREFIX\n"
		"                Write PREFIX.{palette,subset,mix}.ply point clouds.\n"
		"\n"
		"Subset (default: farthest-point spread init, then simulated annealing):\n"
		"  --subset-init LIST\n"
		"  --subset-init=LIST\n"
		"                Initial subset: 16 comma-separated palette indices in pen\n"
		"                order (pen 0 first). Default: farthest-point spread.\n"
		"  --subset-from FILE\n"
		"  --subset-from=FILE\n"
		"                Initial subset from a .W16 bundle (subset bytes after magic).\n"
		"  --fix PEN,IDX   Fix pen PEN to palette index IDX (repeatable).\n"
		"  --fix=PEN,IDX   Same; multiple pairs: --fix=0,0,5,217\n"
		"  --lambda=L    Blend (1-L)*e1 + L*e2 per index (default 0.3).\n"
		"  --hist FILE   Sparse histogram: lines \"index count\".\n"
		"  --bayer=2|4   Weight granularity: 2 -> step 4 (2x2 tile), 4 -> step 1 (default).\n"
		"  --weight-granularity=N\n"
		"                Each pen weight is a multiple of N (N divides 16; default 1).\n"
		"\n"
		"Simulated annealing:\n"
		"  --sa-iter=N       Max iterations (default 100; 0 = keep initial subset).\n"
		"  --sa-log-every=N  Progress line every N iters (default 10).\n"
		"  --sa-seed=N       RNG seed (default: time).\n"
		"  --sa-t0=F         Initial temperature (default: auto).\n"
		"  --sa-cool=F       Temperature multiplier per iter (default 0.9995).\n"
		"\n"
		"Without -o, prints C-style weight rows for all 256 indices to stdout.\n"
		"\n"
		"Examples:\n"
		"  %s -o out.w16 --lambda=0.3\n"
		"  %s -o out.w16 -c --sa-iter=500\n"
		"  %s -o SATSEL.W16 -p SATSEL.PAL --sa-iter=0\n"
		"  %s -p TEMPERAT.PAL --sa-iter=0\n"
		"  %s --dump-ply /tmp/temperat -o temperat.w16\n",
		prog, prog, prog, prog, prog, prog);
}

static void print_usage(const char *prog)
{
	fprintf(stderr, "Try '%s --help' for more information.\n", prog);
}

static int set_weight_granularity(int gran)
{
	if (gran <= 0 || (PALETTE_OPT_WEIGHT_SUM % gran) != 0) {
		fprintf(stderr,
			"error: weight granularity must be a positive divisor of %d\n",
			PALETTE_OPT_WEIGHT_SUM);
		return 0;
	}
	palette_opt_weight_granularity = gran;
	return 1;
}

static int parse_bayer_option(const char *value)
{
	if (!strcmp(value, "2") || !strcmp(value, "2x2"))
		return set_weight_granularity(PALETTE_OPT_WEIGHT_GRANULARITY_2X2_BAYER);
	if (!strcmp(value, "4") || !strcmp(value, "4x4"))
		return set_weight_granularity(1);
	fprintf(stderr, "error: --bayer must be 2, 2x2, 4, or 4x4 (got %s)\n", value);
	return 0;
}

static int parse_weight_granularity_option(const char *value)
{
	char *end = NULL;
	const long gran = strtol(value, &end, 10);

	if (!value || !*value || (end && *end != '\0') || gran <= 0 || gran > PALETTE_OPT_WEIGHT_SUM)
		return 0;
	return set_weight_granularity((int)gran);
}

static void print_subset_comment(FILE *out, const char *label)
{
	int i;
	fprintf(out, "// %s (%d):", label, subset_count);
	for (i = 0; i < subset_count; i++)
		fprintf(out, " %u", (unsigned)subset[i]);
	fprintf(out, "\n");
}

static int apply_subset_spread(int n, const PaletteSubsetFix *fix)
{
	const int got = palette_subset_spread_colors_fix(colors, n, fix, subset);
	if (got != n) {
		fprintf(stderr, "error: spread subset expected %d indices, got %d\n", n, got);
		return 0;
	}
	subset_count = got;
	print_subset_comment(stderr, "spread subset");
	return 1;
}

static int subset_has_dup(const unsigned char *s, int n)
{
	int i, j;
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (s[i] == s[j])
				return 1;
		}
	}
	return 0;
}

static int parse_subset_init_list(const char *csv, int n, const PaletteSubsetFix *fix);

static int load_subset_from_w16(const char *path, unsigned char *out_subset, int n,
	const PaletteSubsetFix *fix)
{
	FILE *f;
	unsigned char hdr[4 + 16];
	int pen;

	if (n != 16) {
		fprintf(stderr, "error: W16 subset requires size 16 (have %d)\n", n);
		return 0;
	}

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open W16 file: %s\n", path);
		return 0;
	}
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fclose(f);
		fprintf(stderr, "error: short read reading subset from %s\n", path);
		return 0;
	}
	fclose(f);

	if (memcmp(hdr, W16_MAGIC, 4) != 0) {
		fprintf(stderr, "error: %s: bad magic (expected W16)\n", path);
		return 0;
	}

	memcpy(out_subset, hdr + 4, 16);
	if (subset_has_dup(out_subset, n)) {
		fprintf(stderr, "error: W16 subset in %s contains duplicate indices\n", path);
		return 0;
	}
	if (fix && !palette_subset_fix_matches_subset(fix, out_subset, n)) {
		for (pen = 0; pen < n; pen++) {
			if (palette_subset_fix_is_fixed(fix, pen) &&
				(int)out_subset[pen] != fix->palette_index[pen]) {
				fprintf(stderr,
					"error: W16 subset pen %d is %u but --fix requires index %d\n",
					pen, (unsigned)out_subset[pen], fix->palette_index[pen]);
				return 0;
			}
		}
	}
	return 1;
}

static int apply_subset_init(int n, const char *subset_init_csv, const char *subset_from_path,
	const PaletteSubsetFix *fix)
{
	const PaletteSubsetFix *fix_arg =
		fix && palette_subset_fix_count(fix) > 0 ? fix : NULL;

	if (subset_from_path && subset_init_csv) {
		fprintf(stderr, "error: use only one of --subset-from and --subset-init\n");
		return 0;
	}
	if (subset_from_path) {
		if (!load_subset_from_w16(subset_from_path, subset, n, fix_arg))
			return 0;
		subset_count = n;
		fprintf(stderr, "subset init: %s\n", subset_from_path);
		return 1;
	}
	if (subset_init_csv) {
		if (!parse_subset_init_list(subset_init_csv, n, fix_arg))
			return 0;
		fprintf(stderr, "subset init: user list\n");
		return 1;
	}
	return 0;
}

static int parse_subset_init_list(const char *csv, int n, const PaletteSubsetFix *fix)
{
	const char *p = csv;
	int count = 0;

	while (*p && count < n) {
		char *end = NULL;
		long v = strtol(p, &end, 10);
		if (end == p)
			break;
		if (v < 0 || v > 255) {
			fprintf(stderr, "error: subset-init index out of range: %ld\n", v);
			return 0;
		}
		subset[count++] = (unsigned char)v;
		if (!*end)
			break;
		p = end;
		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;
	}

	if (count != n) {
		fprintf(stderr, "error: subset-init needs %d indices (pen order), got %d\n", n, count);
		return 0;
	}
	if (subset_has_dup(subset, n)) {
		fprintf(stderr, "error: subset-init contains duplicate indices\n");
		return 0;
	}
	if (fix && !palette_subset_fix_matches_subset(fix, subset, n)) {
		int pen;
		for (pen = 0; pen < n; pen++) {
			if (palette_subset_fix_is_fixed(fix, pen) &&
				(int)subset[pen] != fix->palette_index[pen]) {
				fprintf(stderr,
					"error: subset-init pen %d is %u but --fix requires index %d\n",
					pen, (unsigned)subset[pen], fix->palette_index[pen]);
				return 0;
			}
		}
	}
	subset_count = n;
	return 1;
}

static int dump_ply_palette_subset(const char *prefix)
{
	if (!ply_dump_palette(prefix, colors, raw_pal)) {
		fprintf(stderr, "error: cannot write %s.palette.ply\n", prefix);
		return 0;
	}
	fprintf(stderr, "wrote %s.palette.ply\n", prefix);

	if (!ply_dump_subset(prefix, colors, raw_pal, subset, subset_count)) {
		fprintf(stderr, "error: cannot write %s.subset.ply\n", prefix);
		return 0;
	}
	fprintf(stderr, "wrote %s.subset.ply\n", prefix);
	return 1;
}

int main(int argc, const char **argv)
{
	const char *palette_path = NULL;
	const char *output_path = NULL;
	const char *ply_prefix = NULL;
	const char *hist_path = NULL;
	const char *subset_init_csv = NULL;
	const char *subset_from_path = NULL;
	int continue_mode = 0;
	int argi = 1;
	int subset_n = 16;
	unsigned char loaded_pal[768];
	unsigned char all_weights[256][16];
	double alpha[PALETTE_OPT_NUM_COLORS];
	float lambda = 0.3f;
	PaletteSubsetOptParams sa_params;
	PaletteSubsetFix subset_fix;
	FILE *outf = NULL;
	int print_weights;
	int target;

	palette_subset_fix_clear(&subset_fix);
	palette_subset_opt_params_default(&sa_params);
	palette_hist_uniform(alpha);

	while (argi < argc) {
		if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
			print_help(stdout, argv[0]);
			return 0;
		} else if (!strcmp(argv[argi], "-o") || !strcmp(argv[argi], "--output")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			output_path = argv[++argi];
		} else if (!strncmp(argv[argi], "-o", 2) && argv[argi][2]) {
			output_path = argv[argi] + 2;
		} else if (!strncmp(argv[argi], "--output=", 9)) {
			output_path = argv[argi] + 9;
		} else if (!strcmp(argv[argi], "-c") || !strcmp(argv[argi], "--continue")) {
			continue_mode = 1;
		} else if (!strcmp(argv[argi], "-p") || !strcmp(argv[argi], "--palette")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			palette_path = argv[++argi];
		} else if (!strcmp(argv[argi], "--dump-ply")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			ply_prefix = argv[++argi];
		} else if (!strncmp(argv[argi], "--lambda=", 9)) {
			lambda = (float)atof(argv[argi] + 9);
		} else if (!strcmp(argv[argi], "--hist")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			hist_path = argv[++argi];
		} else if (!strncmp(argv[argi], "--hist=", 7)) {
			hist_path = argv[argi] + 7;
		} else if (!strncmp(argv[argi], "--subset-init=", 14)) {
			subset_init_csv = argv[argi] + 14;
		} else if (!strcmp(argv[argi], "--subset-init")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			subset_init_csv = argv[++argi];
		} else if (!strncmp(argv[argi], "--subset-from=", 14)) {
			subset_from_path = argv[argi] + 14;
		} else if (!strcmp(argv[argi], "--subset-from")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			subset_from_path = argv[++argi];
		} else if (!strncmp(argv[argi], "--sa-iter=", 10)) {
			sa_params.sa_max_iter = atoi(argv[argi] + 10);
		} else if (!strncmp(argv[argi], "--sa-seed=", 10)) {
			sa_params.sa_seed = (unsigned int)strtoul(argv[argi] + 10, NULL, 10);
		} else if (!strncmp(argv[argi], "--sa-t0=", 8)) {
			sa_params.sa_t0 = (float)atof(argv[argi] + 8);
		} else if (!strncmp(argv[argi], "--sa-cool=", 10)) {
			sa_params.sa_cool = (float)atof(argv[argi] + 10);
		} else if (!strncmp(argv[argi], "--sa-log-every=", 15)) {
			sa_params.sa_log_every = atoi(argv[argi] + 15);
		} else if (!strncmp(argv[argi], "--bayer=", 8)) {
			if (!parse_bayer_option(argv[argi] + 8))
				return 1;
		} else if (!strcmp(argv[argi], "--bayer")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			if (!parse_bayer_option(argv[++argi]))
				return 1;
		} else if (!strncmp(argv[argi], "--weight-granularity=", 21)) {
			if (!parse_weight_granularity_option(argv[argi] + 21)) {
				fprintf(stderr, "error: invalid --weight-granularity value: %s\n",
					argv[argi] + 21);
				return 1;
			}
		} else if (!strcmp(argv[argi], "--weight-granularity")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			if (!parse_weight_granularity_option(argv[++argi])) {
				fprintf(stderr, "error: invalid --weight-granularity value: %s\n",
					argv[argi]);
				return 1;
			}
		} else if (!strncmp(argv[argi], "--fix=", 6)) {
			if (palette_subset_fix_parse_pair(&subset_fix, argv[argi] + 6) != 0) {
				fprintf(stderr, "error: invalid --fix= value: %s\n", argv[argi] + 6);
				return 1;
			}
		} else if (!strcmp(argv[argi], "--fix")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			if (palette_subset_fix_parse_pair(&subset_fix, argv[++argi]) != 0) {
				fprintf(stderr, "error: invalid --fix value: %s\n", argv[argi]);
				return 1;
			}
		} else {
			fprintf(stderr, "error: unknown option: %s\n", argv[argi]);
			print_usage(argv[0]);
			return 1;
		}
		argi++;
	}

	if (continue_mode && !output_path) {
		fprintf(stderr, "error: -c/--continue requires -o/--output\n");
		return 1;
	}
	if (continue_mode && (subset_from_path || subset_init_csv)) {
		fprintf(stderr,
			"error: -c/--continue cannot be combined with --subset-from or --subset-init\n");
		return 1;
	}
	if (subset_n <= 0 || subset_n > PALETTE_SUBSET_MAX) {
		fprintf(stderr, "error: subset size must be 1..%d\n", PALETTE_SUBSET_MAX);
		return 1;
	}

	if (output_path) {
		if (continue_mode) {
			if (!file_exists(output_path)) {
				fprintf(stderr, "error: -c/--continue: %s does not exist\n", output_path);
				return 1;
			}
		} else if (file_exists(output_path)) {
			fprintf(stderr, "error: %s already exists (use -c/--continue to update)\n",
				output_path);
			return 1;
		}
	}

	if (palette_subset_fix_count(&subset_fix) > 0) {
		if (palette_subset_fix_validate(&subset_fix, subset_n) != 0) {
			fprintf(stderr, "error: invalid --fix assignments\n");
			return 1;
		}
		palette_subset_fix_log(&subset_fix, stderr);
	}

	if (palette_path && !load_palette_file(palette_path, loaded_pal))
		return 1;

	{
		int i;
		for (i = 0; i < 768; i++)
			raw_pal[i] = palette_path ? loaded_pal[i] : PLAYPAL[i];
	}

	palette_build_opt_colors(raw_pal, colors);
	palette_build_dist_sq_matrix(colors, dist_sq);
	sa_params.lambda = lambda;

	if (hist_path) {
		if (palette_hist_load(hist_path, alpha) != 0) {
			fprintf(stderr, "error: failed to load histogram: %s\n", hist_path);
			return 1;
		}
		fprintf(stderr, "histogram: loaded %s (normalized)\n", hist_path);
	} else {
		fprintf(stderr, "histogram: uniform (no --hist)\n");
	}

	if (palette_opt_weight_granularity == 1) {
		fprintf(stderr, "weights: granularity 1 (4x4 Bayer)\n");
	} else {
		fprintf(stderr, "weights: granularity %d (%d slots per tile, rows still sum to %d)\n",
			palette_opt_weight_granularity,
			PALETTE_OPT_WEIGHT_SUM / palette_opt_weight_granularity,
			PALETTE_OPT_WEIGHT_SUM);
	}

	if (continue_mode) {
		if (!load_subset_from_w16(output_path, subset, subset_n,
				palette_subset_fix_count(&subset_fix) > 0 ? &subset_fix : NULL))
			return 1;
		subset_count = subset_n;
		fprintf(stderr, "subset init: continue from %s\n", output_path);
	} else if (!apply_subset_init(subset_n, subset_init_csv, subset_from_path, &subset_fix)) {
		if (!apply_subset_spread(subset_n,
				palette_subset_fix_count(&subset_fix) > 0 ? &subset_fix : NULL))
			return 1;
		fprintf(stderr, "subset init: farthest-point spread\n");
	}

	{
		PaletteSubsetOptStats stats;
		const PaletteSubsetFix *fix_arg =
			palette_subset_fix_count(&subset_fix) > 0 ? &subset_fix : NULL;
		if (palette_subset_opt_anneal(colors, dist_sq, subset, subset_n, fix_arg, alpha,
				&sa_params, &stats, stderr) != 0) {
			fprintf(stderr, "error: subset simulated annealing failed\n");
			return 1;
		}
		subset_count = subset_n;
		print_subset_comment(stderr, "opt subset");
	}

	if (ply_prefix && !dump_ply_palette_subset(ply_prefix))
		return 1;

	print_weights = (output_path == NULL);

	memset(all_weights, 0, sizeof(all_weights));

	for (target = 0; target < 256; target++) {
		unsigned char wrow[16];
		const float residual = palette_weight_opt_best(colors, dist_sq, subset, subset_count,
			target, lambda, wrow);

		if (print_weights) {
			int i;
			printf("{");
			for (i = 0; i < 16; i++) {
				if (i)
					printf(" ");
				printf("%u", (unsigned)wrow[i]);
			}
			printf("}, // %d: %f\n", target, residual);
		}
		memcpy(all_weights[target], wrow, 16);
	}

	if (output_path) {
		if (subset_count != 16) {
			fprintf(stderr, "error: .W16 output requires a 16-entry subset (have %d)\n",
				subset_count);
			return 1;
		}
		outf = fopen(output_path, "wb");
		if (!outf) {
			fprintf(stderr, "error: cannot open output: %s\n", output_path);
			return 1;
		}
		if (fwrite(W16_MAGIC, 1, 4, outf) != 4) {
			fprintf(stderr, "error: cannot write magic to %s\n", output_path);
			fclose(outf);
			return 1;
		}
		if (fwrite(subset, 1, 16, outf) != 16) {
			fprintf(stderr, "error: cannot write subset to %s\n", output_path);
			fclose(outf);
			return 1;
		}
		for (target = 0; target < 256; target++) {
			if (fwrite(all_weights[target], 1, 16, outf) != 16) {
				fprintf(stderr, "error: cannot write weights to %s\n", output_path);
				fclose(outf);
				return 1;
			}
		}
		fprintf(stderr, "wrote %s (%d bytes)\n", output_path, W16_FILE_BYTES);
		fclose(outf);
	}

	if (ply_prefix) {
		if (!ply_dump_mix(ply_prefix, colors, raw_pal, all_weights, subset, subset_count))
			return 1;
		fprintf(stderr, "wrote %s.mix.ply\n", ply_prefix);
	}

	return 0;
}
