/*
 * stvq_prof.c - 200 Hz timing helpers (Supexec read of _hz_200 @ 0x4BA).
 */
#include "stvq_prof.h"

#include <mint/osbind.h>

#include <string.h>

static long hz200_super(void)
{
	return (long)(*(volatile unsigned long *)0x4BAL);
}

unsigned long stvq_hz200(void)
{
	return (unsigned long)Supexec(hz200_super);
}

void stvq_prof_reset(StvqProf *p)
{
	memset(p, 0, sizeof(*p));
}

static void acc(unsigned long *sum, unsigned long *mx, unsigned long v)
{
	*sum += v;
	if (v > *mx)
		*mx = v;
}

void stvq_prof_add(StvqProf *p)
{
	p->frames++;
	acc(&p->sum_read, &p->max_read, p->last_read);
	acc(&p->sum_stcr, &p->max_stcr, p->last_stcr);
	acc(&p->sum_decode, &p->max_decode, p->last_decode);
	acc(&p->sum_wait, &p->max_wait, p->last_wait);
	acc(&p->sum_present, &p->max_present, p->last_present);
	acc(&p->sum_audio, &p->max_audio, p->last_audio);
	acc(&p->sum_total, &p->max_total, p->last_total);
	p->sum_stfr_bytes += p->last_stfr_bytes;
	p->sum_stcr_n += p->last_stcr_n;
	p->sum_pcm_bytes += p->last_pcm_bytes;
}

/* 1 _hz200 tick = 5 ms. Print avg as ms with one decimal, max as whole ms. */
static void print_bucket(FILE *out, const char *name, unsigned long sum, unsigned long mx, unsigned long n)
{
	unsigned long avg10 = (sum * 50u) / n; /* tenths of a millisecond */

	fprintf(out, "  %-8s avg %lu.%lu ms  max %lu ms\n", name, avg10 / 10u, avg10 % 10u, mx * 5u);
}

void stvq_prof_print(const StvqProf *p, FILE *out)
{
	unsigned long n = p->frames ? p->frames : 1;

	fprintf(out, "\n--- STVQ profile (%lu frames) ---\n", p->frames);
	fprintf(out, "Late present (>=1 VBL): %lu / %lu frames\n", p->late_present, p->frames);

	/* Order matches the play loop: decode -> wait -> audio -> present. */
	fprintf(out, "Buckets (avg / max):\n");
	print_bucket(out, "read", p->sum_read, p->max_read, n);
	print_bucket(out, "stcr", p->sum_stcr, p->max_stcr, n);
	print_bucket(out, "decode", p->sum_decode, p->max_decode, n);
	print_bucket(out, "wait", p->sum_wait, p->max_wait, n);
	print_bucket(out, "audio", p->sum_audio, p->max_audio, n);
	print_bucket(out, "present", p->sum_present, p->max_present, n);
	print_bucket(out, "TOTAL", p->sum_total, p->max_total, n);

	fprintf(out, "Payload avg %lu B/frame  STCR avg %lu  PCM avg %lu B\n",
	    p->sum_stfr_bytes / n,
	    p->sum_stcr_n / n,
	    p->sum_pcm_bytes / n);
}
