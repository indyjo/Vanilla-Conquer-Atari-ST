/*
 * stvq_prof.h - 200 Hz (_hz_200 @ 0x4BA) frame timing buckets.
 * stvq_hz200() reads the sysvar directly; caller must be in supervisor mode.
 */
#ifndef STVQ_PROF_H
#define STVQ_PROF_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	STVQ_HZ200_PER_SEC = 200,
	/* 50 Hz display: one VBL = 20 ms = 4 * 5 ms ticks. */
	STVQ_HZ200_PER_VBL = 4
};

typedef struct StvqProf {
	unsigned long frames;

	/* Per-frame last sample (ticks). */
	unsigned long last_read;
	unsigned long last_stcr;
	unsigned long last_decode;
	unsigned long last_wait;
	unsigned long last_present;
	unsigned long last_audio;
	unsigned long last_total;

	/* Sums / maxima across frames. */
	unsigned long sum_read, max_read;
	unsigned long sum_stcr, max_stcr;
	unsigned long sum_decode, max_decode;
	unsigned long sum_wait, max_wait;
	unsigned long sum_present, max_present;
	unsigned long sum_audio, max_audio;
	unsigned long sum_total, max_total;

	/* I/O shape for last frame / totals. */
	unsigned long last_stfr_bytes;
	unsigned long sum_stfr_bytes;
	unsigned long last_stcr_n;
	unsigned long sum_stcr_n;
	unsigned long last_pcm_bytes;
	unsigned long sum_pcm_bytes;

	/* Nominal frame period in _hz200 ticks (from clip fps). */
	unsigned long budget_ticks;
	/* Present-to-present gap > budget + 1 VBL. */
	unsigned long late_present;
} StvqProf;

unsigned long stvq_hz200(void);

void stvq_prof_reset(StvqProf *p);
void stvq_prof_add(StvqProf *p);
void stvq_prof_print(const StvqProf *p, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_PROF_H */
