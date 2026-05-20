#include "palette_color.h"
#include "ply_dump.h"
#include "subset_spread.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

extern unsigned char PLAYPAL[];

float colors[768];
static unsigned char raw_pal[768];

static unsigned char subset[PALETTE_SUBSET_MAX];
static int subset_count = 16;

static const unsigned char kDefaultSubset[16] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

typedef float vec3[3];

float veclength(vec3 v)
{
	return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void vecdiff(float *r, const float *a, const float *b)
{
	int i;
	for (i = 0; i < 3; i++)
		r[i] = a[i] - b[i];
}

// Finds an optimal approximation of a given color vector using a weighted sum of palette colors.
// weight_left: how many weight units are still to be distributed?
// delta: distance vector to target color.
// fixed_palette_weights: number of palette entries whose weights have already been fixed.
// remaining_colors: maximum number of remaining colors to choose from.
// retrace: whether or not to output the result to the console.
float find_best_dist(int weight_left, vec3 delta, const float *target, float accum_dev,
	int fixed_palette_weights, int remaining_colors, char retrace, unsigned char *out_weights)
{
	if (fixed_palette_weights == subset_count) {
		if (weight_left > 0)
			return -1;
		return 2.0f * veclength(delta) + 3.0f * accum_dev;
	}

	int best_weight = 0;
	float best_dist = -1.0f;
	float *colorvec = colors + 3 * subset[fixed_palette_weights];
	vec3 color_delta;

	vecdiff(color_delta, target, colorvec);
	{
		const float dev_contrib = veclength(color_delta);

		if (remaining_colors > 0) {
			int w;
			for (w = weight_left; w >= 0; w--) {
				vec3 remaining_delta;
				int c;
				for (c = 0; c < 3; c++)
					remaining_delta[c] = delta[c] - (float)w * colorvec[c];

				{
					const float dist = find_best_dist(weight_left - w, remaining_delta, target,
						accum_dev + (w == 0 ? 0.0f : dev_contrib),
						fixed_palette_weights + 1,
						remaining_colors - (w == 0 ? 0 : 1), 0, NULL);
					if (dist >= 0.0f && (best_dist < 0.0f || dist < best_dist)) {
						best_dist = dist;
						best_weight = w;
					}
				}
			}
		}

		if (retrace) {
			int c;
			printf(" %d,", best_weight);
			fflush(stdout);
			if (out_weights != NULL)
				out_weights[fixed_palette_weights] = (unsigned char)best_weight;

			for (c = 0; c < 3; c++)
				delta[c] -= (float)best_weight * colorvec[c];
			find_best_dist(weight_left - best_weight, delta, target,
				accum_dev + (best_weight == 0 ? 0.0f : dev_contrib),
				fixed_palette_weights + 1,
				remaining_colors - (best_weight == 0 ? 0 : 1), 1, out_weights);
		}
	}
	return best_dist;
}

#define MAX_COLORS 4
#define MAX_WEIGHT 16

#define W16_MAGIC "W16"
#define W16_FILE_BYTES (4 + 16 + 256 * 16)

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
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

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-p palette.{PAL|WSA}] [begin end] [--dump out.w16]\n"
		"       %s [--dump-ply PREFIX] [--subset-spread[=N]] [--subset-spread-only[=N]]\n",
		prog, prog);
}

static void print_subset_comment(FILE *out, const char *label)
{
	int i;
	fprintf(out, "// %s (%d):", label, subset_count);
	for (i = 0; i < subset_count; i++)
		fprintf(out, " %u", (unsigned)subset[i]);
	fprintf(out, "\n");
}

static int apply_subset_spread(int n)
{
	const int got = palette_subset_spread_colors(colors, n, subset);
	if (got != n) {
		fprintf(stderr, "error: spread subset expected %d indices, got %d\n", n, got);
		return 0;
	}
	subset_count = got;
	print_subset_comment(stderr, "spread subset");
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
	const char *dump_path = NULL;
	const char *ply_prefix = NULL;
	int begin = 0;
	int end = 256;
	int argi = 1;
	int use_subset_spread = 0;
	int subset_spread_only = 0;
	int subset_n = 16;
	unsigned char loaded_pal[768];
	unsigned char all_weights[256][16];
	FILE *dumpf = NULL;

	memcpy(subset, kDefaultSubset, sizeof(kDefaultSubset));

	while (argi < argc) {
		if (!strcmp(argv[argi], "-p") || !strcmp(argv[argi], "--palette")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			palette_path = argv[++argi];
		} else if (!strcmp(argv[argi], "--dump")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			dump_path = argv[++argi];
		} else if (!strcmp(argv[argi], "--dump-ply")) {
			if (argi + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			ply_prefix = argv[++argi];
		} else if (!strncmp(argv[argi], "--subset-spread=", 16)) {
			use_subset_spread = 1;
			subset_n = atoi(argv[argi] + 16);
		} else if (!strcmp(argv[argi], "--subset-spread")) {
			use_subset_spread = 1;
		} else if (!strncmp(argv[argi], "--subset-spread-only=", 22)) {
			use_subset_spread = 1;
			subset_spread_only = 1;
			subset_n = atoi(argv[argi] + 22);
		} else if (!strcmp(argv[argi], "--subset-spread-only")) {
			use_subset_spread = 1;
			subset_spread_only = 1;
		} else if (begin == 0 && end == 256) {
			begin = atoi(argv[argi]);
			if (argi + 1 < argc && argv[argi + 1][0] != '-') {
				end = atoi(argv[argi + 1]);
				argi++;
			}
		} else {
			print_usage(argv[0]);
			return 1;
		}
		argi++;
	}

	if (use_subset_spread && (subset_n <= 0 || subset_n > PALETTE_SUBSET_MAX)) {
		fprintf(stderr, "error: subset size must be 1..%d\n", PALETTE_SUBSET_MAX);
		return 1;
	}

	if (palette_path && !load_palette_file(palette_path, loaded_pal))
		return 1;

	{
		int i;
		for (i = 0; i < 768; i++)
			raw_pal[i] = palette_path ? loaded_pal[i] : PLAYPAL[i];
	}

	palette_build_opt_colors(raw_pal, colors);

	if (use_subset_spread) {
		if (!apply_subset_spread(subset_n))
			return 1;
		if (subset_spread_only) {
			int i;
			printf("unsigned char subset[] = {");
			for (i = 0; i < subset_count; i++) {
				if (i)
					printf(",");
				printf(" %u", (unsigned)subset[i]);
			}
			printf(" };\n");
			if (ply_prefix && !dump_ply_palette_subset(ply_prefix))
				return 1;
			return 0;
		}
	}

	if (ply_prefix && !dump_ply_palette_subset(ply_prefix))
		return 1;

	if (dump_path) {
		dumpf = fopen(dump_path, "wb");
		if (!dumpf) {
			fprintf(stderr, "error: cannot open dump output: %s\n", dump_path);
			return 1;
		}
	}

	memset(all_weights, 0, sizeof(all_weights));

	{
		int target;
		for (target = begin; target < end; target++) {
			unsigned char wrow[16];
			vec3 delta;
			const float *color = &colors[3 * target];
			int c;
			int i;

			printf("{");
			fflush(stdout);
			for (c = 0; c < 3; c++)
				delta[c] = color[c] * (float)MAX_WEIGHT;
			for (i = 0; i < 16; i++)
				wrow[i] = 0;

			{
				const float residual = find_best_dist(MAX_WEIGHT, delta, color, 0.0f, 0,
					MAX_COLORS, 1, wrow);
				printf("}, // %d: %f\n", target, residual);
			}
			memcpy(all_weights[target], wrow, 16);
		}
	}

	if (dumpf) {
		int target;

		if (subset_count != 16) {
			fprintf(stderr, "error: .W16 dump requires a 16-entry subset (have %d)\n", subset_count);
			fclose(dumpf);
			return 1;
		}
		if (fwrite(W16_MAGIC, 1, 4, dumpf) != 4) {
			fprintf(stderr, "error: cannot write magic to %s\n", dump_path);
			fclose(dumpf);
			return 1;
		}
		if (fwrite(subset, 1, 16, dumpf) != 16) {
			fprintf(stderr, "error: cannot write subset to %s\n", dump_path);
			fclose(dumpf);
			return 1;
		}
		for (target = 0; target < 256; target++) {
			if (fwrite(all_weights[target], 1, 16, dumpf) != 16) {
				fprintf(stderr, "error: cannot write weights to %s\n", dump_path);
				fclose(dumpf);
				return 1;
			}
		}
		fprintf(stderr, "wrote %s (%d bytes)\n", dump_path, W16_FILE_BYTES);
		fclose(dumpf);
	}

	if (ply_prefix) {
		if (!ply_dump_mix(ply_prefix, colors, raw_pal, all_weights, subset, subset_count))
			return 1;
		fprintf(stderr, "wrote %s.mix.ply\n", ply_prefix);
	}

	return 0;
}
