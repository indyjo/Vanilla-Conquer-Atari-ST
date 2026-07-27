/*
 * stvq_encode.h - VQA → STVQ encode orchestration.
 */
#ifndef STVQ_ENCODE_H
#define STVQ_ENCODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Optional encode progress. phase is "prep" (tile raster) or "encode" (STVQ write).
 * done is 1-based frames completed; 0 means starting a phase with known total.
 */
typedef void (*StvqProgressFn)(void *ctx, const char *phase, unsigned done, unsigned total);

typedef struct StvqEncodeOpts {
	const char *vqa_path;
	const char *out_path;
	unsigned cb_size;
	unsigned cb_per_frame;
	unsigned cb_random_pct; /* % of STCR slots accepted as uniform random (rest: utility) */
	unsigned cb_lookahead;  /* extra frames after current used for eviction/utility */
	float dct_alpha;        /* w(u,v)=1/(1+alpha*(u^2+v^2)); <0 → default */
	unsigned dct_coeffs;    /* Y zig-zag DCT coeffs; 0 → default */
	unsigned dct_chroma_coeffs; /* U and V each; use have_dct_chroma */
	int have_dct_chroma;    /* 1 if dct_chroma_coeffs was set (incl. 0 = Y-only) */
	float gamma;            /* palette-opt YUV gamma; <0 → default */
	int dry_run;
	/*
	 * Optional CRC-named W16 root. When set with w16_crc, load
	 * {w16_dir}/video/{crc:08x}.{seg}.w16 instead of stem-next-to-VQA.
	 */
	const char *w16_dir;
	uint32_t w16_crc; /* lowercase hex CRC; 0 = use stem sidecars */
	int have_w16_crc;
	StvqProgressFn progress;
	void *progress_ctx;
} StvqEncodeOpts;

int stvq_encode(const StvqEncodeOpts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_ENCODE_H */
