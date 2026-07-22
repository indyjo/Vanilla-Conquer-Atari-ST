/*
 * stvq_encode.h - VQA → STVQ encode orchestration.
 */
#ifndef STVQ_ENCODE_H
#define STVQ_ENCODE_H

#ifdef __cplusplus
extern "C" {
#endif

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
} StvqEncodeOpts;

int stvq_encode(const StvqEncodeOpts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_ENCODE_H */
