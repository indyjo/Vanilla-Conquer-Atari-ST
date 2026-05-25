/*
 * ply_dump.h - ASCII Stanford PLY point clouds for palette-opt visualization.
 */

#ifndef PALETTE_OPT_PLY_DUMP_H
#define PALETTE_OPT_PLY_DUMP_H

/*
 * Write palette-opt metric-space points (x,y,z) with uchar RGB from pal768
 * (VGA 6-bit expanded to 0..255). palette_index[i] labels each vertex.
 */
int ply_write_points(const char *path, int count, const float *metric_xyz,
	const unsigned char *pal768, const int *palette_index);

/* PREFIX.palette.ply — all 256 source colors in metric space. */
int ply_dump_palette(const char *prefix, const float *colors, const unsigned char *pal768);

/* PREFIX.subset.ply — subset pens (subset[] indices). */
int ply_dump_subset(const char *prefix, const float *colors, const unsigned char *pal768,
	const unsigned char *subset, int subset_count);

/* PREFIX.mix.ply — per-index dither mix centroids (weights sum to 16). */
int ply_dump_mix(const char *prefix, const float *colors, const unsigned char *pal768,
	const unsigned char (*weights)[16], const unsigned char *subset, int subset_count);

#endif /* PALETTE_OPT_PLY_DUMP_H */
