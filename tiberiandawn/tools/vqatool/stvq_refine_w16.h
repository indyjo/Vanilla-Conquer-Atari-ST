/*
 * stvq_refine_w16.h - Continue-mode W16 refinement via palette-opt.
 */
#ifndef STVQ_REFINE_W16_H
#define STVQ_REFINE_W16_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum StvqRefineMode {
	STVQ_REFINE_NORMAL = 0, /* --sa-t0=1e-4 (default) */
	STVQ_REFINE_QUICK = 1,  /* --sa-t0=1.5e-5 */
	STVQ_REFINE_THOROUGH = 2 /* palette-opt SA defaults */
} StvqRefineMode;

typedef struct StvqRefineW16Opts {
	const char *vqa_path;
	const char *palette_opt;
	const char *palette_opt_args; /* comma-separated extras, may be NULL */
	StvqRefineMode mode;
	int dry_run;
} StvqRefineW16Opts;

int stvq_refine_w16(const StvqRefineW16Opts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_REFINE_W16_H */
