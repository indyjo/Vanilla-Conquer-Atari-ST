/*
 * Deterministic WSA playback bench. See st_wsa_bench.h.
 */

#include "st_wsa_bench.h"

#include "c2p.h"
#include "function.h"
#include "gbuffer.h"
#include "palette.h"
#include "st_mix_register.h"
#include "wsa.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <string.h>

/* Europe zoom, 320x200. The file has 92 frames; the bench plays the first 30. */
static char const ST_WSA_BENCH_NAME[] = "EARTH_E.WSA";
static int const ST_WSA_BENCH_FRAMES = 30;
static int const ST_WSA_PAGE_WIDTH = 320;
static int const ST_WSA_PAGE_HEIGHT = 200;

/*
 * Golden page checksums (sum = sum * 65599 + byte, wrapping at 32 bits).
 * Chunky: linear page after each Animate_Frame. Planar: persistent ST page
 * after each Blit, starting from a cleared page. Recompute only when the
 * playback pixels are supposed to change.
 */
static unsigned const ST_WSA_BENCH_CHUNKY_SUM = 249212043u;
static unsigned const ST_WSA_BENCH_PLANAR_SUM = 701780649u;

static unsigned st_wsa_fold(unsigned sum, unsigned char const *bytes, int count)
{
	for (int i = 0; i < count; i++) {
		sum = sum * 65599u + bytes[i];
	}
	return sum;
}

static unsigned long st_wsa_hz200(void)
{
	return *(volatile unsigned long *)0x4BAL;
}

int st_run_wsa_bench(void)
{
	unsigned char pal[768];
	GraphicBufferClass chunky;
	GraphicBufferClass planar;
	void *anim = NULL;
	int rc = 1;

	printf("\n-- WSA bench (%s, %d frames) --\n", ST_WSA_BENCH_NAME, ST_WSA_BENCH_FRAMES);
	st_tests_register_mixes_once();

	if (!CCFileClass(ST_WSA_BENCH_NAME).Is_Available()) {
		printf("wsa_bench: SKIP %s not in a registered mix\n", ST_WSA_BENCH_NAME);
		return 2;
	}

	chunky.Init(ST_WSA_PAGE_WIDTH, ST_WSA_PAGE_HEIGHT, NULL, 0, (int)GBC_NONE);
	planar.Init(ST_WSA_PAGE_WIDTH, ST_WSA_PAGE_HEIGHT, NULL, 0, (int)GBC_ST_PLANAR_LORES);
	if (!chunky.Get_Buffer() || !planar.Get_Buffer()) {
		printf("wsa_bench: FAIL allocation\n");
		return 1;
	}

	WSAOpenType const flags = (WSAOpenType)(WSA_OPEN_FROM_DISK | WSA_OPEN_TO_PAGE | WSA_DEFERRED_C2P_WEIGHTSET);
	anim = Open_Animation(ST_WSA_BENCH_NAME, NULL, 0L, flags, pal);
	if (!anim) {
		printf("wsa_bench: FAIL Open_Animation\n");
		return 1;
	}

	int const frames = Get_Animation_Frame_Count(anim);
	int const aw = Get_Animation_Width(anim);
	int const ah = Get_Animation_Height(anim);
	int const ax = Get_Animation_X(anim);
	int const ay = Get_Animation_Y(anim);
	if (frames < ST_WSA_BENCH_FRAMES || aw != ST_WSA_PAGE_WIDTH || ah != ST_WSA_PAGE_HEIGHT || ax != 0 || ay != 0) {
		printf("wsa_bench: FAIL asset %s is %d frames %dx%d at %d,%d\n",
			ST_WSA_BENCH_NAME, frames, aw, ah, ax, ay);
		Close_Animation(anim);
		return 1;
	}

	Install_Animation_C2P_WeightSet(anim);
	if (!C2P_Weights_Are_Ready()) {
		printf("wsa_bench: FAIL no C2P weights for %s\n", ST_WSA_BENCH_NAME);
		Close_Animation(anim);
		return 1;
	}

	/* Palette, the 200 Hz clock, and the BLiTTER are supervisor-only.
	 * Disk WSA reads stay inside this section; EmuTOS allows GEMDOS there. */
	long const ssp = Super(0L);
	Set_Palette(pal);

	chunky.Clear(0);
	planar.Clear(0);

	unsigned chunky_sum = 0;
	unsigned planar_sum = 0;
	unsigned long decode_ticks = 0;
	unsigned long c2p_ticks = 0;
	int const chunky_bytes = ST_WSA_PAGE_WIDTH * ST_WSA_PAGE_HEIGHT;
	int const planar_bytes = 160 * ST_WSA_PAGE_HEIGHT;
	unsigned char const *chunky_bytes_ptr = (unsigned char const *)chunky.Get_Buffer();
	unsigned char const *planar_bytes_ptr = (unsigned char const *)planar.Get_Buffer();

	for (int frame = 0; frame < ST_WSA_BENCH_FRAMES; frame++) {
		unsigned long const td0 = st_wsa_hz200();
		int const ok = Animate_Frame(anim, chunky, frame, 0, 0, WSA_NORMAL, NULL, NULL);
		unsigned long const td1 = st_wsa_hz200();
		if (!ok) {
			SuperToUser(ssp);
			printf("wsa_bench: FAIL Animate_Frame %d\n", frame);
			Close_Animation(anim);
			return 1;
		}
		decode_ticks += td1 - td0;

		unsigned long const tc0 = st_wsa_hz200();
		chunky.Blit(planar, FALSE);
		unsigned long const tc1 = st_wsa_hz200();
		c2p_ticks += tc1 - tc0;

		chunky_sum = st_wsa_fold(chunky_sum, chunky_bytes_ptr, chunky_bytes);
		planar_sum = st_wsa_fold(planar_sum, planar_bytes_ptr, planar_bytes);
	}

	SuperToUser(ssp);

	unsigned long const ticks = decode_ticks + c2p_ticks;
	printf("wsa_bench: file=%s frames=%d/%d size=%dx%d\n",
		ST_WSA_BENCH_NAME, ST_WSA_BENCH_FRAMES, frames, aw, ah);
	printf("wsa_bench: decode_ticks=%lu c2p_ticks=%lu ticks=%lu\n",
		decode_ticks, c2p_ticks, ticks);
	printf("wsa_bench: chunky=%u planar=%u\n", chunky_sum, planar_sum);

	if (chunky_sum != ST_WSA_BENCH_CHUNKY_SUM || planar_sum != ST_WSA_BENCH_PLANAR_SUM) {
		printf("wsa_bench: FAIL chunky expect %u planar expect %u\n",
			ST_WSA_BENCH_CHUNKY_SUM, ST_WSA_BENCH_PLANAR_SUM);
		rc = 1;
	} else {
		printf("wsa_bench: PASS\n");
		rc = 0;
	}

	Close_Animation(anim);
	fflush(stdout);
	return rc;
}
