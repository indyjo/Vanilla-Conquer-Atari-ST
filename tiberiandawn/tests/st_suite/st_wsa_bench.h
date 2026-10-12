/*
 * Deterministic WSA playback bench.
 *
 * Plays a fixed prefix of EARTH_E.WSA through Animate_Frame (disk, direct to
 * the chunky page) and GraphicViewPortClass::Blit (chunky to planar). Checksums
 * lock the chunky page and the persistent planar page after every frame.
 * ticks is the 200 Hz cost of those two calls only.
 */
#ifndef ST_WSA_BENCH_H
#define ST_WSA_BENCH_H

/* 0 = pass, 1 = fail, 2 = skip (animation or mixes not present). */
int st_run_wsa_bench(void);

#endif
