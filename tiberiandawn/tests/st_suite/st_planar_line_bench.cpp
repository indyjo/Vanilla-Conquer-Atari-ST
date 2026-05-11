#include "st_planar_line_bench.h"

#include "st_planar_draw.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const unsigned long ST_BENCH_PIXELS_TARGET = (1UL << 20);

static inline unsigned long st_read_hz200(void)
{
#if defined(ATARI_ST)
	return *(volatile unsigned long *)0x4BAL;
#else
	return 0UL;
#endif
}

int st_run_planar_line_bench(void)
{
	static const short k_lengths[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
	static const short k_width = 320;
	static const short k_height = 200;
	static const short k_row_words = (short)(160 / 2);

	const size_t words = (size_t)k_row_words * (size_t)k_height;
	uint16_t *buf = (uint16_t *)malloc(words * sizeof(uint16_t));
	if (!buf) {
		printf("Planar line bench: allocation failed\n");
		return 1;
	}
	memset(buf, 0, words * sizeof(uint16_t));

	long old_ssp = Super(0L);
	printf("\n-- Planar line benchmark (200Hz) --\n");
	printf("Target pixels per case: %lu\n", ST_BENCH_PIXELS_TARGET);
	printf("Lengths: 1,2,4,8,16,32,64,128\n\n");

	printf("HLINE:\n");
	for (unsigned i = 0; i < sizeof(k_lengths) / sizeof(k_lengths[0]); ++i) {
		const short len = k_lengths[i];
		const unsigned long reps = (ST_BENCH_PIXELS_TARGET + (unsigned long)len - 1UL) / (unsigned long)len;
		unsigned long t0 = st_read_hz200();
		for (unsigned long r = 0; r < reps; ++r) {
			const short y = (short)(r % (unsigned long)k_height);
			const short max_x1 = (short)(k_width - len);
			const short x1 = (short)((r * 13UL) % (unsigned long)(max_x1 + 1));
			const short x2 = (short)(x1 + len - 1);
			ST_Planar_Draw_HLine_Fast(buf, k_row_words, y, x1, x2, 0x0F);
		}
		unsigned long dt = st_read_hz200() - t0;
		const unsigned long pixels = reps * (unsigned long)len;
		printf("  len=%3d reps=%8lu pixels=%9lu ticks=%6lu\n",
			(int)len, reps, pixels, dt);
	}

	printf("VLINE:\n");
	for (unsigned i = 0; i < sizeof(k_lengths) / sizeof(k_lengths[0]); ++i) {
		const short len = k_lengths[i];
		const unsigned long reps = (ST_BENCH_PIXELS_TARGET + (unsigned long)len - 1UL) / (unsigned long)len;
		unsigned long t0 = st_read_hz200();
		for (unsigned long r = 0; r < reps; ++r) {
			const short x = (short)(r % (unsigned long)k_width);
			const short max_y1 = (short)(k_height - len);
			const short y1 = (short)((r * 7UL) % (unsigned long)(max_y1 + 1));
			const short y2 = (short)(y1 + len - 1);
			ST_Planar_Draw_VLine_Fast(buf, k_row_words, x, y1, y2, 0x0F);
		}
		unsigned long dt = st_read_hz200() - t0;
		const unsigned long pixels = reps * (unsigned long)len;
		printf("  len=%3d reps=%8lu pixels=%9lu ticks=%6lu\n",
			(int)len, reps, pixels, dt);
	}

	SuperToUser(old_ssp);
	free(buf);
	return 0;
}
