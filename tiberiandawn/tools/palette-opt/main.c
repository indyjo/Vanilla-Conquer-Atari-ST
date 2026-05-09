#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// 768 bytes
extern unsigned char PLAYPAL[];

float colors[768];

/* First 16 palette indices (EGA-style ramp in C&C). */
unsigned char subset[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

typedef float vec3[3];

float veclength(vec3 v) {
	return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
}

void vecdiff(float *r, const float *a, const float *b) {
	for (int i=0; i<3; i++) r[i] = a[i]-b[i];
}

// Finds an optimal approximation of a given color vector using a weighted sum of palette colors.
// weight_left: how many weight units are still to be distruibuted?
// delta: distance vector to target color.
// fixed_palette_weights: number of palette entries whose weights have already been fixed.
// remaining_colors: maximum number of remaining colors to choose from.
// retrace: whether or not to output the result to the console.
float find_best_dist(int weight_left, vec3 delta, const float *target, float accum_dev, int fixed_palette_weights, int remaining_colors, char retrace, unsigned char *out_weights) {
	if (fixed_palette_weights == sizeof(subset)) {
		if (weight_left > 0) {
			//printf("Error: no more palette weights remaining and weight left: %d\n", weight_left);
			return -1;
		}
		return 2*veclength(delta) + 3*accum_dev;
	}

	int best_weight = 0;
	float best_dist = -1;
	float *colorvec = colors + 3*subset[fixed_palette_weights];

	vec3 color_delta;
	vecdiff(color_delta, target, colorvec);
	// The value this color would contribute to the deviation if chosen.
	float dev_contrib = veclength(color_delta);

	if (remaining_colors > 0) {
		for (int w = weight_left; w >= 0; w--) {
			vec3 remaining_delta;
			for (int c=0; c<3; c++) remaining_delta[c] = delta[c] - w*colorvec[c];

			float dist = find_best_dist(weight_left - w,
				remaining_delta,
				target,
				accum_dev + (w==0?0.0f:dev_contrib),
				fixed_palette_weights + 1,
				remaining_colors - (w==0?0:1),
				0,
				NULL);
			if (dist >= 0 && (best_dist < 0 || dist < best_dist)) {
				best_dist = dist;
				best_weight = w;
			}
		}
	}

	if (retrace) {
		// Retrace best result
		printf(" %d,", best_weight);
		fflush(stdout);
		if (out_weights != NULL) {
			out_weights[fixed_palette_weights] = (unsigned char)best_weight;
		}
	
		for (int c = 0; c<3; c++) delta[c] -= best_weight * colorvec[c];
		find_best_dist(weight_left - best_weight, delta, target,
			accum_dev + (best_weight==0?0.0f:dev_contrib),
			fixed_palette_weights + 1, remaining_colors - (best_weight==0?0:1), 1, out_weights);
	}
	return best_dist;
}

// Do not try to mix more colors than this
#define MAX_COLORS 4
// 16 is the right number for a 4x4 Bayer matrix
#define MAX_WEIGHT 16

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

	/* Raw PAL file (RGB6 triplets). */
	if (size == 768) {
		if (fread(dst768, 1, 768, f) != 768) {
			fclose(f);
			fprintf(stderr, "error: short read for PAL file %s\n", path);
			return 0;
		}
		fclose(f);
		return 1;
	}

	/* WSA with embedded palette: [14-byte header][(NrOfFrames+2) offsets][768 palette] */
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

int main(int argc, const char** argv) {
	const char *palette_path = NULL;
	const char *dump_path = NULL;
	int begin = 0;
	int end = 256;
	int argi = 1;
	unsigned char loaded_pal[768];
	FILE *dumpf = NULL;

	while (argi < argc) {
		if (!strcmp(argv[argi], "-p") || !strcmp(argv[argi], "--palette")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "usage: %s [-p palette.{PAL|WSA}] [begin end] [--dump out.w16]\n", argv[0]);
				return 1;
			}
			palette_path = argv[++argi];
		} else if (!strcmp(argv[argi], "--dump")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "usage: %s [-p palette.{PAL|WSA}] [begin end] [--dump out.w16]\n", argv[0]);
				return 1;
			}
			dump_path = argv[++argi];
		} else if (begin == 0 && end == 256) {
			begin = atoi(argv[argi]);
			if (argi + 1 < argc && argv[argi + 1][0] != '-') {
				end = atoi(argv[argi + 1]);
				argi++;
			}
		} else {
			fprintf(stderr, "usage: %s [-p palette.{PAL|WSA}] [begin end] [--dump out.w16]\n", argv[0]);
			return 1;
		}
		argi++;
	}

	if (palette_path) {
		if (!load_palette_file(palette_path, loaded_pal)) {
			return 1;
		}
	}

	// Apply gamma to emphasize the darker parts
	/* C&C .PAL channels are 0-63 (VGA 6-bit). */
	for (int i=0; i<768; i++) {
		const unsigned char v = palette_path ? loaded_pal[i] : PLAYPAL[i];
		colors[i] = powf(v / 63.0f, 1.6f);
	}
	// Convert to YUV in order to emphasize Y (luma) over UV (chroma)
	for (int i=0; i<256; i++) {
		float r = colors[3*i + 0];
		float g = colors[3*i + 1];
		float b = colors[3*i + 2];
		float y = 0.299f*r + 0.587f*g + 0.114f*b;
		float u = 0.492f*(b-y);
		float v = 0.877f*(r-y);
		colors[3*i + 0] = 2*y; // emphasize y
		colors[3*i + 1] = u;
		colors[3*i + 2] = v;
	}
	if (dump_path) {
		dumpf = fopen(dump_path, "wb");
		if (!dumpf) {
			fprintf(stderr, "error: cannot open dump output: %s\n", dump_path);
			return 1;
		}
	}
	for (int target=begin; target<end; target++) {
		unsigned char wrow[16];
		printf("{");
		fflush(stdout);
		vec3 delta;
		const float *color = &colors[3*target];
		for (int c=0; c<3; c++) delta[c]=color[c] * MAX_WEIGHT;
		for (int i = 0; i < 16; i++) {
			wrow[i] = 0;
		}
		float residual = find_best_dist(MAX_WEIGHT, delta, color, 0.0f, 0, MAX_COLORS, 1, wrow);
		printf("}, // %d: %f\n", target, residual);
		if (dumpf) {
			fwrite(wrow, 1, 16, dumpf);
		}
	}
	if (dumpf) {
		fclose(dumpf);
	}
	return 0;
}


