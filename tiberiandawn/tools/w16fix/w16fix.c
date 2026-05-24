/*
 * w16fix.c - Reorder C2P .W16 subset pens and remap weight columns.
 *
 * For each palette index N in the subset with 0 <= N < 16, assign pen N := N.
 * Remaining subset colors (palette index >= 16) fill unused pens sorted by
 * ascending VGA palette index.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W16_MAGIC "W16"
#define W16_FILE_BYTES (4 + 16 + 256 * 16)

typedef struct {
	unsigned char subset[16];
	unsigned char weights[256][16];
} W16Data;

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

static int load_w16(const char *path, W16Data *out)
{
	unsigned char hdr[4 + 16];
	FILE *f;
	size_t n;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	n = fread(hdr, 1, sizeof(hdr), f);
	if (n != sizeof(hdr)) {
		fprintf(stderr, "error: short read in %s\n", path);
		fclose(f);
		return 0;
	}
	if (memcmp(hdr, W16_MAGIC, 4) != 0) {
		fprintf(stderr, "error: %s: bad magic (expected W16)\n", path);
		fclose(f);
		return 0;
	}
	memcpy(out->subset, hdr + 4, 16);
	for (int src = 0; src < 256; src++) {
		if (fread(out->weights[src], 1, 16, f) != 16) {
			fprintf(stderr, "error: short read weights row %d in %s\n", src, path);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	if (subset_has_dup(out->subset, 16)) {
		fprintf(stderr, "error: subset in %s contains duplicate indices\n", path);
		return 0;
	}
	for (int src = 0; src < 256; src++) {
		int sum = 0;
		for (int k = 0; k < 16; k++)
			sum += (int)out->weights[src][k];
		if (sum != 16) {
			fprintf(stderr, "error: weights row %d sums to %d (expected 16)\n", src, sum);
			return 0;
		}
	}
	return 1;
}

static int save_w16(const char *path, const W16Data *data)
{
	FILE *f;

	f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "error: cannot write %s\n", path);
		return 0;
	}
	if (fwrite(W16_MAGIC, 1, 4, f) != 4 || fwrite(data->subset, 1, 16, f) != 16) {
		fprintf(stderr, "error: cannot write header to %s\n", path);
		fclose(f);
		return 0;
	}
	for (int src = 0; src < 256; src++) {
		if (fwrite(data->weights[src], 1, 16, f) != 16) {
			fprintf(stderr, "error: cannot write weights row %d to %s\n", src, path);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	return 1;
}

static int subset_contains(const unsigned char *subset, int n, int idx)
{
	int i;
	for (i = 0; i < n; i++) {
		if ((int)subset[i] == idx)
			return 1;
	}
	return 0;
}

static int old_pen_for_palette(const unsigned char *subset, int pal)
{
	int k;
	for (k = 0; k < 16; k++) {
		if ((int)subset[k] == pal)
			return k;
	}
	return -1;
}

static int pal_asc_cmp(const void *a, const void *b)
{
	return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

static void log_subset(FILE *log, const char *label, const unsigned char *subset)
{
	int i;
	fprintf(log, "%s:", label);
	for (i = 0; i < 16; i++)
		fprintf(log, " %u", (unsigned)subset[i]);
	fputc('\n', log);
}

static int fix_w16(W16Data *io)
{
	unsigned char new_subset[16];
	unsigned char new_weights[256][16];
	int pen_used[16];
	unsigned char remain[16];
	int remain_count = 0;
	int next_pen;

	memset(pen_used, 0, sizeof(pen_used));

	for (int n = 0; n < 16; n++) {
		if (subset_contains(io->subset, 16, n)) {
			new_subset[n] = (unsigned char)n;
			pen_used[n] = 1;
		}
	}

	for (int k = 0; k < 16; k++) {
		const int pal = (int)io->subset[k];
		if (pal < 16)
			continue;
		remain[remain_count++] = (unsigned char)pal;
	}

	qsort(remain, (size_t)remain_count, sizeof(remain[0]), pal_asc_cmp);

	next_pen = 0;
	for (int i = 0; i < remain_count; i++) {
		while (next_pen < 16 && pen_used[next_pen])
			next_pen++;
		if (next_pen >= 16) {
			fprintf(stderr, "error: not enough free pens for remaining subset colors\n");
			return 0;
		}
		new_subset[next_pen] = remain[i];
		pen_used[next_pen] = 1;
		fprintf(stderr, "  pen %2d <- palette %3u\n", next_pen, (unsigned)remain[i]);
		next_pen++;
	}

	for (int src = 0; src < 256; src++) {
		for (int new_pen = 0; new_pen < 16; new_pen++) {
			const int pal = (int)new_subset[new_pen];
			const int old_pen = old_pen_for_palette(io->subset, pal);
			if (old_pen < 0) {
				fprintf(stderr, "error: palette %d missing from old subset\n", pal);
				return 0;
			}
			new_weights[src][new_pen] = io->weights[src][old_pen];
		}
	}

	log_subset(stderr, "old subset", io->subset);
	memcpy(io->subset, new_subset, 16);
	memcpy(io->weights, new_weights, sizeof(new_weights));
	log_subset(stderr, "new subset", io->subset);
	return 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-o OUT] FILE.W16\n"
		"\n"
		"Reorder subset pens so palette indices 0..15 that appear in the subset\n"
		"occupy matching pen slots. Remaining colors (index >= 16) fill other pens\n"
		"in ascending palette-index order. Weight rows are permuted to match.\n"
		"\n"
		"Without -o, FILE.W16 is updated in place.\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *in_path = NULL;
	const char *out_path = NULL;
	W16Data data;
	int argi;

	for (argi = 1; argi < argc; argi++) {
		if (!strcmp(argv[argi], "-o") || !strcmp(argv[argi], "--output")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a path\n", argv[argi]);
				return 1;
			}
			out_path = argv[++argi];
		} else if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (argv[argi][0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", argv[argi]);
			usage(argv[0]);
			return 1;
		} else if (in_path) {
			fprintf(stderr, "error: unexpected argument %s\n", argv[argi]);
			usage(argv[0]);
			return 1;
		} else {
			in_path = argv[argi];
		}
	}

	if (!in_path) {
		usage(argv[0]);
		return 1;
	}
	if (!out_path)
		out_path = in_path;

	if (!load_w16(in_path, &data))
		return 1;

	fprintf(stderr, "%s:\n", in_path);
	if (!fix_w16(&data))
		return 1;

	if (!save_w16(out_path, &data)) {
		return 1;
	}
	fprintf(stderr, "wrote %s (%d bytes)\n", out_path, W16_FILE_BYTES);
	return 0;
}
