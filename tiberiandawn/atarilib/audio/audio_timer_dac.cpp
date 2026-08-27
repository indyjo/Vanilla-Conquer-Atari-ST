/*
 * audio_timer_dac.cpp - MFP Timer A ~6.25 kHz digi (YM movep LUT / Covox Port B).
 *
 * Soft 1024-byte ring (TT-RAM OK), **2048-byte aligned**. Play cursor in USP.
 * Digi_Submit converts signed PCM → device-native and optional ÷2 from 12500.
 * YM: s&$FC into signed-aware LUT (silence=mid). Covox: s^$80.
 * ISRs consume native bytes only (stride 1).
 */
#ifdef ATARI_ST

#include "audio/audio_timer_dac.h"
#include "audio/digi_audio.h"
#include "audio/digi_ring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	YM_REG_SEL = 0xFFFF8800UL,
	YM_REG_DAT = 0xFFFF8802UL,
	MFP_IERA = 0xFFFFFA07UL,
	MFP_IPRA = 0xFFFFFA0BUL,
	MFP_ISRA = 0xFFFFFA0FUL,
	MFP_IMRA = 0xFFFFFA13UL,
	MFP_TACR = 0xFFFFFA19UL,
	MFP_TADR = 0xFFFFFA1FUL,
	MFP_VEC_TIMERA = 0x134UL,
	TIMER_A_DATA = 98,
	TIMER_A_CTRL_DIV4 = 1,
	TIMER_DAC_RING_BYTES = DIGI_RING_BYTES,
	RING_ALIGN = 2048,
	RING_WRAP_AND = ((~(RING_ALIGN - 1)) | (TIMER_DAC_RING_BYTES - 1)) & 0xFFFF,
	DEVICE_RATE_HZ = 6250u
};

static DigiRing g_ring;
static unsigned char* g_ring_mem;
static StAudioDriver g_sink = ST_AUDIO_NONE;
static unsigned char* g_saved_usp;
static volatile int g_timer_on;
static volatile int g_paused;
static void (*g_old_timera)(void);
static unsigned char g_saved_iera;
static unsigned char g_saved_imra;
static unsigned char g_saved_tacr;
static unsigned char g_ym_mixer_save;
static unsigned char g_ym_vol_save[3];
static unsigned char g_ym_porta_save;
static unsigned char g_ym_portb_save;
static int g_inited;
static int g_usp_hijacked;
static DigiInfo g_info;
static unsigned char g_submit_scratch[512];

static_assert((TIMER_DAC_RING_BYTES & (TIMER_DAC_RING_BYTES - 1)) == 0,
    "timer ring size must be power of two");
static_assert(TIMER_DAC_RING_BYTES <= RING_ALIGN,
    "ring must fit in one RING_ALIGN page for fused-pointer wrap");
static_assert(RING_WRAP_AND == 0xFBFF,
    "unexpected wrap mask (expect 1024@2048 → 0xFBFF)");

static void ym_write_reg(unsigned char reg, unsigned char val)
{
	*((volatile unsigned char*)YM_REG_SEL) = reg;
	*((volatile unsigned char*)YM_REG_DAT) = val;
}

static unsigned char ym_read_reg(unsigned char reg)
{
	*((volatile unsigned char*)YM_REG_SEL) = reg;
	return *((volatile unsigned char*)YM_REG_DAT);
}

static void ym_chip_claim(void)
{
	int i;
	g_ym_mixer_save = ym_read_reg(7);
	for (i = 0; i < 3; ++i) {
		g_ym_vol_save[i] = ym_read_reg((unsigned char)(8 + i));
	}
	g_ym_porta_save = ym_read_reg(14);
	g_ym_portb_save = ym_read_reg(15);
	ym_write_reg(0, 0);
	ym_write_reg(1, 0);
	ym_write_reg(2, 0);
	ym_write_reg(3, 0);
	ym_write_reg(4, 0);
	ym_write_reg(5, 0);
	ym_write_reg(7, (unsigned char)((g_ym_mixer_save & 0xC0u) | 0x3Fu));
	ym_write_reg(8, 0);
	ym_write_reg(9, 0);
	ym_write_reg(10, 0);
	if (g_sink == ST_AUDIO_COVOX) {
		ym_write_reg(7, (unsigned char)((ym_read_reg(7) & (unsigned char)~0x80u) | 0x3Fu));
		ym_write_reg(15, 0x80u);
	}
}

static void ym_chip_restore(void)
{
	ym_write_reg(8, g_ym_vol_save[0]);
	ym_write_reg(9, g_ym_vol_save[1]);
	ym_write_reg(10, g_ym_vol_save[2]);
	ym_write_reg(14, g_ym_porta_save);
	ym_write_reg(15, g_ym_portb_save);
	ym_write_reg(7, g_ym_mixer_save);
}

#if defined(__GNUC__) && defined(__m68k__)
static unsigned char* play_get(void)
{
	unsigned char* p;
	__asm__ __volatile__("move.l %%usp,%0" : "=a"(p));
	return p;
}

static void play_set(unsigned char* p)
{
	__asm__ __volatile__("move.l %0,%%usp" : : "a"(p) : "memory");
}
#else
static unsigned char* g_play_fallback;
static unsigned char* play_get(void)
{
	return g_play_fallback;
}
static void play_set(unsigned char* p)
{
	g_play_fallback = p;
}
#endif

static unsigned timer_consumer_pos(DigiRing* r)
{
	unsigned char* const base = r && r->base ? r->base : g_ring.base;
	unsigned char* const play = play_get();
	if (!base || !play) {
		return 0;
	}
	return (unsigned)(play - base);
}

static void timer_arm(DigiRing* r)
{
	play_set((r && r->base) ? r->base : g_ring.base);
	g_paused = 0;
	*((volatile unsigned char*)MFP_TACR) = 0;
	*((volatile unsigned char*)MFP_TADR) = (unsigned char)TIMER_A_DATA;
	*((volatile unsigned char*)MFP_IERA) = (unsigned char)(*((volatile unsigned char*)MFP_IERA) | 0x20u);
	*((volatile unsigned char*)MFP_IMRA) = (unsigned char)(*((volatile unsigned char*)MFP_IMRA) | 0x20u);
	*((volatile unsigned char*)MFP_TACR) = (unsigned char)TIMER_A_CTRL_DIV4;
	g_timer_on = 1;
}

static void timer_stop(DigiRing* r)
{
	(void)r;
	*((volatile unsigned char*)MFP_TACR) = 0;
	*((volatile unsigned char*)MFP_IERA) = (unsigned char)(*((volatile unsigned char*)MFP_IERA) & (unsigned char)~0x20u);
	*((volatile unsigned char*)MFP_IMRA) = (unsigned char)(*((volatile unsigned char*)MFP_IMRA) & (unsigned char)~0x20u);
	g_timer_on = 0;
	g_paused = 1;
}

static DigiRingOps const g_timer_ops = {timer_consumer_pos, timer_arm, timer_stop};

#if defined(__GNUC__) && defined(__m68k__)
extern "C" void timer_a_ym_s1(void);

#define TIMER_ISR_COVOX \
	"movem.l %%d0-%%d1/%%a0,-(%%sp)\n\t" \
	"move.l %%usp,%%a0\n\t" \
	"move.w #0x0F00,%%d0\n\t" \
	"move.b (%%a0)+,%%d0\n\t" \
	"move.l %%a0,%%d1\n\t" \
	"lea 0xffff8800.w,%%a0\n\t" \
	"movep.w %%d0,0(%%a0)\n\t" \
	"move.l %%d1,%%d0\n\t" \
	"andi.w #0xFBFF,%%d0\n\t" \
	"move.l %%d0,%%a0\n\t" \
	"move.l %%a0,%%usp\n\t" \
	"move.b #0xDF,0xfffffa0f.w\n\t" \
	"movem.l (%%sp)+,%%d0-%%d1/%%a0\n\t" \
	"rte"

static void timer_a_covox_s1(void)
{
	__asm__ __volatile__(TIMER_ISR_COVOX : : : "d0", "d1", "a0", "memory", "cc");
}

static void timer_install_active_isr(void)
{
	if (g_sink == ST_AUDIO_YM) {
		*(void (**)(void))MFP_VEC_TIMERA = timer_a_ym_s1;
	} else {
		*(void (**)(void))MFP_VEC_TIMERA = timer_a_covox_s1;
	}
}
#else
static void timer_a_stub(void)
{
	*((volatile unsigned char*)MFP_ISRA) = (unsigned char)~0x20u;
}
static void timer_install_active_isr(void)
{
	*(void (**)(void))MFP_VEC_TIMERA = timer_a_stub;
}
#endif

static unsigned char silence_byte(void)
{
	return (g_sink == ST_AUDIO_COVOX) ? (unsigned char)0x80u : (unsigned char)0;
}

static int source_needs_half(unsigned rate_flags)
{
	/* ÷2 only when source is 12.5 kHz and this device is ~6.25 kHz. */
	return (rate_flags & DIGI_RATE_12500) != 0 && g_info.device_rate_hz < 10000u;
}

static DigiInfo const* timer_digi_info(void)
{
	return &g_info;
}

static unsigned timer_digi_capacity(unsigned rate_flags)
{
	unsigned freeb;
	if (!g_inited || !g_ring.base) {
		return 0;
	}
	freeb = digi_ring_free_bytes(&g_ring);
	if (source_needs_half(rate_flags)) {
		return freeb * 2u;
	}
	return freeb;
}

static void const* timer_digi_submit_ym(void const* start, void const* end, unsigned rate_flags)
{
	unsigned char const* s = (unsigned char const*)start;
	unsigned char const* e = (unsigned char const*)end;
	unsigned const half = source_needs_half(rate_flags) ? 1u : 0u;

	if (!g_inited || !g_ring.base || !s || e <= s) {
		return start;
	}

	while (s < e) {
		unsigned freeb = digi_ring_free_bytes(&g_ring);
		unsigned i;
		unsigned out_n;
		unsigned produced;

		if (freeb == 0) {
			break;
		}
		if (freeb > sizeof(g_submit_scratch)) {
			freeb = (unsigned)sizeof(g_submit_scratch);
		}

		if (half) {
			unsigned pairs = (unsigned)(e - s) / 2u;
			if (pairs == 0) {
				break;
			}
			if (pairs > freeb) {
				pairs = freeb;
			}
			out_n = pairs;
			for (i = 0; i < out_n; ++i) {
				g_submit_scratch[i] = (unsigned char)(s[i * 2u] & 0xFCu);
			}
			produced = digi_ring_write_available(&g_ring, g_submit_scratch, out_n);
			s += produced * 2u;
			if (produced < out_n) {
				break;
			}
		} else {
			unsigned src_n = (unsigned)(e - s);
			if (src_n > freeb) {
				src_n = freeb;
			}
			out_n = src_n;
			for (i = 0; i < out_n; ++i) {
				g_submit_scratch[i] = (unsigned char)(s[i] & 0xFCu);
			}
			produced = digi_ring_write_available(&g_ring, g_submit_scratch, out_n);
			s += produced;
			if (produced < out_n) {
				break;
			}
		}
	}
	return s;
}

static void const* timer_digi_submit_covox(void const* start, void const* end, unsigned rate_flags)
{
	unsigned char const* s = (unsigned char const*)start;
	unsigned char const* e = (unsigned char const*)end;
	unsigned const half = source_needs_half(rate_flags) ? 1u : 0u;

	if (!g_inited || !g_ring.base || !s || e <= s) {
		return start;
	}

	while (s < e) {
		unsigned freeb = digi_ring_free_bytes(&g_ring);
		unsigned i;
		unsigned out_n;
		unsigned produced;

		if (freeb == 0) {
			break;
		}
		if (freeb > sizeof(g_submit_scratch)) {
			freeb = (unsigned)sizeof(g_submit_scratch);
		}

		if (half) {
			unsigned pairs = (unsigned)(e - s) / 2u;
			if (pairs == 0) {
				break;
			}
			if (pairs > freeb) {
				pairs = freeb;
			}
			out_n = pairs;
			for (i = 0; i < out_n; ++i) {
				g_submit_scratch[i] = (unsigned char)(s[i * 2u] ^ 0x80u);
			}
			produced = digi_ring_write_available(&g_ring, g_submit_scratch, out_n);
			s += produced * 2u;
			if (produced < out_n) {
				break;
			}
		} else {
			unsigned src_n = (unsigned)(e - s);
			if (src_n > freeb) {
				src_n = freeb;
			}
			out_n = src_n;
			for (i = 0; i < out_n; ++i) {
				g_submit_scratch[i] = (unsigned char)(s[i] ^ 0x80u);
			}
			produced = digi_ring_write_available(&g_ring, g_submit_scratch, out_n);
			s += produced;
			if (produced < out_n) {
				break;
			}
		}
	}
	return s;
}

static void timer_digi_pause(void)
{
	timer_stop(&g_ring);
	g_ring.armed = 0;
}

static void timer_digi_resume(void)
{
	g_paused = 0;
}

static void timer_digi_flush(void)
{
	timer_digi_pause();
	digi_ring_reset(&g_ring);
	digi_ring_silence(&g_ring, silence_byte());
	if (g_ring.base) {
		play_set(g_ring.base);
	}
}

static int timer_digi_active(void)
{
	return g_inited && g_ring.armed;
}

static void timer_digi_shutdown(void);

static void digi_install_timer_hooks(void)
{
	Digi_Info = timer_digi_info;
	Digi_Submit = (g_sink == ST_AUDIO_YM) ? timer_digi_submit_ym : timer_digi_submit_covox;
	Digi_Capacity = timer_digi_capacity;
	Digi_Active = timer_digi_active;
	Digi_Pause = timer_digi_pause;
	Digi_Resume = timer_digi_resume;
	Digi_Flush = timer_digi_flush;
	Digi_Shutdown = timer_digi_shutdown;
}

static void timer_digi_shutdown(void)
{
	Timer_Dac_Shutdown();
}

int Timer_Dac_Init(StAudioDriver sink)
{
	if (sink != ST_AUDIO_YM && sink != ST_AUDIO_COVOX) {
		return 0;
	}
	Timer_Dac_Shutdown();

	g_ring_mem = (unsigned char*)malloc((size_t)TIMER_DAC_RING_BYTES + (size_t)RING_ALIGN);
	if (!g_ring_mem) {
		printf("Timer-DAC: ring alloc failed\n");
		fflush(stdout);
		return 0;
	}
	{
		unsigned long const raw = (unsigned long)g_ring_mem;
		unsigned char* const aligned =
		    (unsigned char*)((raw + (unsigned long)RING_ALIGN - 1UL) & ~((unsigned long)RING_ALIGN - 1UL));
		g_sink = sink;
		memset(aligned, (int)silence_byte(), (size_t)TIMER_DAC_RING_BYTES);
		if (!g_usp_hijacked) {
			g_saved_usp = play_get();
			g_usp_hijacked = 1;
		}
		play_set(aligned);
		digi_ring_init(&g_ring, aligned, TIMER_DAC_RING_BYTES, &g_timer_ops);
	}

	ym_chip_claim();
	g_saved_iera = *((volatile unsigned char*)MFP_IERA);
	g_saved_imra = *((volatile unsigned char*)MFP_IMRA);
	g_saved_tacr = *((volatile unsigned char*)MFP_TACR);
	g_old_timera = *(void (**)(void))MFP_VEC_TIMERA;
	timer_install_active_isr();

	g_info.device_rate_hz = DEVICE_RATE_HZ;
	g_info.ring_samples = TIMER_DAC_RING_BYTES;
	g_paused = 1;
	g_inited = 1;
	digi_install_timer_hooks();
	return 1;
}

void Timer_Dac_Shutdown(void)
{
	if (!g_inited && !g_ring_mem) {
		g_sink = ST_AUDIO_NONE;
		Digi_Clear_Hooks();
		return;
	}
	timer_stop(&g_ring);
	if (g_old_timera) {
		*(void (**)(void))MFP_VEC_TIMERA = g_old_timera;
	}
	*((volatile unsigned char*)MFP_IERA) = g_saved_iera;
	*((volatile unsigned char*)MFP_IMRA) = g_saved_imra;
	*((volatile unsigned char*)MFP_TACR) = g_saved_tacr;
	ym_chip_restore();

	free(g_ring_mem);
	g_ring_mem = 0;
	if (g_usp_hijacked) {
		play_set(g_saved_usp);
		g_saved_usp = 0;
		g_usp_hijacked = 0;
	}
	digi_ring_reset(&g_ring);
	g_ring.base = 0;
	g_sink = ST_AUDIO_NONE;
	g_inited = 0;
	g_old_timera = 0;
	Digi_Clear_Hooks();
}

#endif /* ATARI_ST */
