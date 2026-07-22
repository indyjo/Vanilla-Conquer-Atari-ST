/*
 * stvq_fix_w16.h - Run tools/w16fix on each name.<N>.w16 sidecar.
 */
#ifndef STVQ_FIX_W16_H
#define STVQ_FIX_W16_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqFixW16Opts {
	const char *vqa_path;
	const char *w16fix; /* w16fix binary path */
	int dry_run;
} StvqFixW16Opts;

int stvq_fix_w16(const StvqFixW16Opts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_FIX_W16_H */
