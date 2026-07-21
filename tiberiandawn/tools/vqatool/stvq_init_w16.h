/*
 * stvq_init_w16.h - Create missing name.<N>.w16 sidecars via palette-opt.
 */
#ifndef STVQ_INIT_W16_H
#define STVQ_INIT_W16_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqInitW16Opts {
	const char *vqa_path;
	const char *palette_opt;
	int dry_run;
} StvqInitW16Opts;

int stvq_init_w16(const StvqInitW16Opts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_INIT_W16_H */
