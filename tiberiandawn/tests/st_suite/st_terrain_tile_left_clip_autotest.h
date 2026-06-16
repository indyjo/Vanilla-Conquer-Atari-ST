#ifndef ST_TERRAIN_TILE_LEFT_CLIP_AUTOTEST_H
#define ST_TERRAIN_TILE_LEFT_CLIP_AUTOTEST_H

/* Returns number of failed clip cases. verbose: print first mismatches. */
int st_run_terrain_tile_left_clip_autotest_ex(int verbose, int *out_mismatches);

int st_run_terrain_tile_left_clip_autotest(void);

#endif
