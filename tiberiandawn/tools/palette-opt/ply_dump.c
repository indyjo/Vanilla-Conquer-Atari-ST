/*
 * ply_dump.c - Minimal ASCII PLY writer (CloudCompare / MeshLab compatible).
 */

#include "ply_dump.h"

#include <stdio.h>
#include <string.h>

static void palette_index_rgb8(const unsigned char *pal768, int idx, unsigned char rgb[3])
{
	rgb[0] = (unsigned char)((pal768[3 * idx + 0] * 255 + 31) / 63);
	rgb[1] = (unsigned char)((pal768[3 * idx + 1] * 255 + 31) / 63);
	rgb[2] = (unsigned char)((pal768[3 * idx + 2] * 255 + 31) / 63);
}

int ply_write_points(const char *path, int count, const float *metric_xyz,
	const unsigned char *pal768, const int *palette_index)
{
	FILE *f;
	int i;

	if (!path || !metric_xyz || !pal768 || !palette_index || count <= 0)
		return 0;

	f = fopen(path, "w");
	if (!f)
		return 0;

	fprintf(f, "ply\n");
	fprintf(f, "format ascii 1.0\n");
	fprintf(f, "comment palette-opt metric space\n");
	fprintf(f, "element vertex %d\n", count);
	fprintf(f, "property float x\n");
	fprintf(f, "property float y\n");
	fprintf(f, "property float z\n");
	fprintf(f, "property uchar red\n");
	fprintf(f, "property uchar green\n");
	fprintf(f, "property uchar blue\n");
	fprintf(f, "property int palette_index\n");
	fprintf(f, "end_header\n");

	for (i = 0; i < count; i++) {
		const float *p = &metric_xyz[3 * i];
		unsigned char rgb[3];
		const int pidx = palette_index[i];
		palette_index_rgb8(pal768, pidx, rgb);
		fprintf(f, "%.6f %.6f %.6f %u %u %u %d\n",
			p[0], p[1], p[2],
			(unsigned)rgb[0], (unsigned)rgb[1], (unsigned)rgb[2],
			pidx);
	}

	fclose(f);
	return 1;
}

static int path_join3(char *out, size_t outcap, const char *prefix, const char *suffix)
{
	int n = snprintf(out, outcap, "%s%s", prefix, suffix);
	return n > 0 && (size_t)n < outcap;
}

int ply_dump_palette(const char *prefix, const float *colors, const unsigned char *pal768)
{
	char path[1024];
	int pindex[256];
	int i;

	if (!path_join3(path, sizeof(path), prefix, ".palette.ply"))
		return 0;
	for (i = 0; i < 256; i++)
		pindex[i] = i;
	return ply_write_points(path, 256, colors, pal768, pindex);
}

int ply_dump_subset(const char *prefix, const float *colors, const unsigned char *pal768,
	const unsigned char *subset, int subset_count)
{
	char path[1024];
	float xyz[256 * 3];
	int pindex[256];
	int i;

	if (!subset || subset_count <= 0)
		return 0;
	if (!path_join3(path, sizeof(path), prefix, ".subset.ply"))
		return 0;
	for (i = 0; i < subset_count; i++) {
		const int idx = (int)subset[i];
		xyz[3 * i + 0] = colors[3 * idx + 0];
		xyz[3 * i + 1] = colors[3 * idx + 1];
		xyz[3 * i + 2] = colors[3 * idx + 2];
		pindex[i] = idx;
	}
	return ply_write_points(path, subset_count, xyz, pal768, pindex);
}

int ply_dump_mix(const char *prefix, const float *colors, const unsigned char *pal768,
	const unsigned char (*weights)[16], const unsigned char *subset, int subset_count)
{
	char path[1024];
	float xyz[256 * 3];
	int pindex[256];
	int i;
	int k;

	if (!weights || !subset || subset_count <= 0)
		return 0;
	if (!path_join3(path, sizeof(path), prefix, ".mix.ply"))
		return 0;
	for (i = 0; i < 256; i++) {
		float mx = 0.0f;
		float my = 0.0f;
		float mz = 0.0f;
		const unsigned char *wrow = weights[i];
		for (k = 0; k < subset_count; k++) {
			const int w = (int)wrow[k];
			const int idx = (int)subset[k];
			if (w == 0)
				continue;
			mx += (float)w * colors[3 * idx + 0];
			my += (float)w * colors[3 * idx + 1];
			mz += (float)w * colors[3 * idx + 2];
		}
		xyz[3 * i + 0] = mx / 16.0f;
		xyz[3 * i + 1] = my / 16.0f;
		xyz[3 * i + 2] = mz / 16.0f;
		pindex[i] = i;
	}
	return ply_write_points(path, 256, xyz, pal768, pindex);
}
