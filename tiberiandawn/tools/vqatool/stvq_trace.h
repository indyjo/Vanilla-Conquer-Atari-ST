/*
 * stvq_trace.h - Optional STVI encode-decision trace for stvqinspect.
 *
 * Little-endian. Header is 64 bytes. index_off is 0 until the encode finishes;
 * a viewer should reject a file whose index_off is still 0.
 *
 *   0  char magic[4] = "STVI"
 *   4  u16 version = 1
 *   6  u16 width, height, tiles_x, tiles_y, cb_entries, feat_len,
 *      y_coeffs, chroma_coeffs, fps
 *  24  u32 frame_count
 *  28  f32 dct_alpha, chroma_weight, gamma
 *  40  u64 index_off
 *  48  u8  reserved[16]
 *
 * Footer at index_off:
 *   u64 frame_off[frame_count]
 *   u32 snapshot_count
 *   u32 snapshot_frame[snapshot_count]   // frames whose record has STVI_F_FULL
 *
 * Each frame record (post-STCR, post-STVD):
 *   u32 frame_index
 *   u16 segment, nwin, nrep, flags     // bit0 = STVI_F_FULL, bit1 = STVI_F_WIN_RESIDUAL,
 *                                      // bit2 = STVI_F_UTIL
 *   u8  ste_rgb[48]                    // 16 encoded pens, RGB8
 *   nwin times:
 *     u16 seg, pad
 *     u8  pal768[768]                  // VGA6
 *     u8  src64[tiles_n * 64]          // column-major VGA indices (col * tiles_y + row)
 *   u8 recon[tiles_n * 32]             // encoded planar tiles, same order (skip = N-2)
 *   if STVI_F_FULL (codebook after this frame's replaces):
 *     u8  valid[cb_entries]            // 1 = current palette epoch (drawable)
 *     u8  tiles[cb_entries * 32]
 *     f32 feats[cb_entries * feat_len]
 *   nrep times:
 *     u16 index
 *     u8  drawable, pad
 *     u8  tile[32]
 *     f32 feat[feat_len]
 *   tiles_n times:
 *     u16 cb_index
 *     u8  skip, pad
 *     u32 dist, dist_n2                // dist_n2 = 0xffffffff if N-2 was not considered
 *     f32 feat[feat_len]               // dithered tile, current palette
 *   if STVI_F_WIN_RESIDUAL (pre-STCR candidate residual, one row per window frame):
 *     u8  pool[nwin]                   // 1 = this frame supplies codebook candidates
 *     u32 residual[nwin * tiles_n]     // ~0u if that frame is outside the pool
 *                                      // higher = more likely to be shortlisted
 *   if STVI_F_UTIL (stage-2 rankings; absent on older traces):
 *     u16 nscore                       // times the shortlist was ranked
 *     u16 nevent
 *     nevent times:
 *       u16 pass                       // 0 = first ranking
 *       u16 cb_index                   // STVI_CB_NONE if this tile stopped the run
 *       u32 wi                         // window block (frame * tiles_n + tile)
 *       i64 predicted, actual          // add-utility before the run, and at install
 *
 * Feature layout matches stvq_metric: [Y | U | V | tail energies].
 * Distance is SSE of the first (y_coeffs + 2*chroma_coeffs) lanes, plus the
 * sum of the tail lanes (not a squared difference). Scale is 65536.
 */
#ifndef STVQ_TRACE_H
#define STVQ_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STVI_VERSION 1u
#define STVI_F_FULL 1u
#define STVI_F_WIN_RESIDUAL 2u
#define STVI_F_UTIL 4u
#define STVI_DIST_NONE 0xffffffffu
#define STVI_CB_NONE 0xffffu

/* One stage-2 decision. cb_index == STVI_CB_NONE means the run stopped here. */
typedef struct StviUtilEvent {
	uint16_t pass;
	uint16_t cb_index;
	uint32_t wi;
	int64_t predicted;
	int64_t actual;
} StviUtilEvent;

typedef struct StviTrace StviTrace;

typedef struct StviTraceInfo {
	unsigned width, height, tiles_x, tiles_y;
	unsigned cb_entries, feat_len;
	unsigned y_coeffs, chroma_coeffs, fps, frame_count;
	float alpha, chroma_weight, gamma;
} StviTraceInfo;

typedef struct StviWinImage {
	uint16_t seg;
	const uint8_t *pal768; /* 768 VGA6 */
	const uint8_t *src64;  /* tiles_n * 64 */
} StviWinImage;

typedef struct StviReplaceRec {
	uint16_t index;
	uint8_t drawable; /* 1 = current epoch after install */
	const uint8_t *tile; /* 32 */
	const float *feat;   /* feat_len */
} StviReplaceRec;

typedef struct StviFrameIn {
	unsigned frame_index;
	unsigned segment;
	const uint8_t *ste_rgb; /* 48 */
	const StviWinImage *wins;
	unsigned nwin;
	const uint8_t *recon; /* tiles_n * 32 */
	int full;             /* write codebook checkpoint */
	const uint8_t *cb_valid;
	const uint8_t *cb_tiles;
	const float *cb_feats;
	unsigned feat_stride; /* floats between codebook rows */
	const StviReplaceRec *reps;
	unsigned nrep;
	const unsigned *cb_index; /* tiles_n */
	const uint8_t *skip;
	const unsigned *dist;
	const unsigned *dist_n2;
	const float *tile_feats; /* tiles_n * feat_len, tightly packed */
	/* Optional. Both set → STVI_F_WIN_RESIDUAL. Lengths are nwin and nwin*tiles_n. */
	const uint8_t *win_pool;
	const unsigned *win_dist;
	/* Optional. Non-NULL → STVI_F_UTIL. nscore is the ranking-pass count. */
	unsigned nscore;
	const StviUtilEvent *util_events;
	unsigned n_util_events;
} StviFrameIn;

StviTrace *stvi_trace_open(const char *path, const StviTraceInfo *info);
int stvi_trace_frame(StviTrace *tr, const StviFrameIn *in);
/* success=1 writes the footer and patches index_off. */
int stvi_trace_close(StviTrace *tr, int success);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_TRACE_H */
