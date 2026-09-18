/*
 * Streaming STE DMA resample (libsamplerate, host tools only).
 */
#ifndef ST_HOST_RESAMPLE_H
#define ST_HOST_RESAMPLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ST_HOST_DMA_RATE 12517u
#define ST_HOST_SRC_RING 8192u

typedef struct StHostSrc {
	void *state; /* SRC_STATE */
	double ratio;
	unsigned in_rate;
	float in_ring[ST_HOST_SRC_RING];
	unsigned in_n;
	float out_tmp[ST_HOST_SRC_RING];
	signed char pend[ST_HOST_SRC_RING];
	unsigned pend_n;
	int eof;
	uint32_t dither;
	int opened;
} StHostSrc;

unsigned st_host_normalize_rate(unsigned rate);
uint32_t st_host_predicted_dest(uint32_t in_n, unsigned in_rate);

int st_host_src_open(StHostSrc *s, unsigned in_rate);
void st_host_src_close(StHostSrc *s);
unsigned st_host_src_in_space(const StHostSrc *s);
int st_host_src_push_s16(StHostSrc *s, const int16_t *in, unsigned n);
int st_host_src_finish(StHostSrc *s);
/* Pull up to n dest samples. Returns count written (may be 0 if more input needed). */
unsigned st_host_src_pull_s8(StHostSrc *s, signed char *dst, unsigned n);
unsigned st_host_src_pull_u8(StHostSrc *s, unsigned char *dst, unsigned n);

#ifdef __cplusplus
}
#endif

#endif
