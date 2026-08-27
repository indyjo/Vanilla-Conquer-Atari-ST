/*
 * audio_dma.cpp - STE-era DMA 8-bit mono PCM (STE / TT / Falcon).
 *
 * Requires supervisor (startup calls Super(0)). Probed via _MCH/_SND cookies:
 * any DMA-capable machine (_MCH hw != 0) with _SND bit 1. Microwire mixer
 * ($8922) is STE-only; Falcon uses Devconnect() routing.
 * DMA is programmed at 12517 Hz mono 8-bit when _SND allows it; otherwise 25033 Hz.
 * One DIGI_RING_BYTES ring in ST-RAM; DMA is armed once to loop it.
 */
#ifdef ATARI_ST

#include "audio/audio_dma.h"
#include "audio/digi_audio.h"
#include "audio/digi_ring.h"
#include "memflag.h"
#include "st_hw_probe.h"

#include <string.h>
#include <mint/cookie.h>
#include <mint/falcon.h>

/* $FF8921 rate bits rr: 01 = 12517 Hz, 10 = 25033 Hz (mono bit 7 set separately). */
enum {
	AUDIO_DMA_RING_BYTES = DIGI_RING_BYTES,
	DMA_HW_RATE_12517_IDX = 1,
	DMA_HW_RATE_25033_IDX = 2
};

static volatile unsigned char* const STE_DMA_CTRL = (volatile unsigned char*)0xFF8900UL;
static volatile unsigned char* const STE_DMA_MODE = (volatile unsigned char*)0xFF8901UL;
/* Sound mode / sample frequency (Hatari dmaSnd.c): bits 0–1 = rate index, bit 7 = mono. */
static volatile unsigned char* const STE_DMA_SOUND_MODE = (volatile unsigned char*)0xFF8921UL;
enum { STE_DMA_SND_MODE_MONO = 0x80u };
static volatile unsigned char* const STE_DMA_START_H = (volatile unsigned char*)0xFF8903UL;
/* $FF8909/B/D is the READ-ONLY frame address counter (current DMA fetch position). */
static volatile unsigned char const* const STE_DMA_CNT_H = (volatile unsigned char*)0xFF8909UL;
static volatile unsigned char const* const STE_DMA_CNT_M = (volatile unsigned char*)0xFF890BUL;
static volatile unsigned char const* const STE_DMA_CNT_L = (volatile unsigned char*)0xFF890DUL;
/* Writable frame end address -- writes to the counter at $FF8909/B/D are silently dropped. */
static volatile unsigned char* const STE_DMA_END_H = (volatile unsigned char*)0xFF890FUL;

/* STE DMA mixer data ($FFFF8922): many demos poke a small constant; no Microwire framing here. */
static volatile unsigned char* const STE_DMA_MIXER = (volatile unsigned char*)0xFFFF8922UL;

static int g_inited;
static unsigned char g_rate_idx;
static unsigned char* g_pool;
static DigiRing g_ring;
static DigiInfo g_info;

static unsigned char g_tos_sound_mode;
static int g_tos_sound_saved;

static void dma_set_address(volatile unsigned char* high_reg, unsigned long phys)
{
	high_reg[0] = (unsigned char)((phys >> 16) & 0xFFU);
	high_reg[2] = (unsigned char)((phys >> 8) & 0xFFU);
	high_reg[4] = (unsigned char)(phys & 0xFFU);
}

static void dma_stop(void)
{
	if (!g_inited) {
		return;
	}
	if (ST_Hw_Is_Falcon_Class()) {
		*STE_DMA_CTRL &= (unsigned char)~0x03u;
		*STE_DMA_MODE &= (unsigned char)~0x03u;
	} else {
		*STE_DMA_CTRL = 0;
		/* $FF8901 low bits: DMA off (many STE docs: %01/%11 = on; %00 = off). */
		*STE_DMA_MODE = 0;
	}
}

static void dma_mixer_connect(void)
{
	if (!g_inited || !ST_Hw_Is_Ste_Sound_Class()) {
		return;
	}
	*STE_DMA_MIXER = 0x03;
}

static void dma_falcon_matrix_connect(void)
{
	if (!g_inited || !ST_Hw_Is_Falcon_Class()) {
		return;
	}
	(void)Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);
}

static void dma_arm_loop(unsigned char const* first, unsigned len)
{
	unsigned char const mode = (unsigned char)(STE_DMA_SND_MODE_MONO | g_rate_idx);

	dma_stop();
	dma_mixer_connect();
	if (ST_Hw_Is_Falcon_Class()) {
		*STE_DMA_SOUND_MODE =
		    (unsigned char)((*STE_DMA_SOUND_MODE & (unsigned char)~0x87u) | mode);
	} else {
		*STE_DMA_SOUND_MODE = mode;
	}
	unsigned long const s = (unsigned long)first;
	unsigned long const e = s + (unsigned long)len;
	dma_set_address(STE_DMA_START_H, s);
	dma_set_address(STE_DMA_END_H, e);
	/* $FF8901 bits 0+1: %11 = play with loop (auto-reload start/end at end-of-sweep). */
	if (ST_Hw_Is_Falcon_Class()) {
		*STE_DMA_MODE = (unsigned char)(*STE_DMA_MODE | 0x03u);
		*STE_DMA_CTRL = (unsigned char)(*STE_DMA_CTRL | 0x03u);
	} else {
		*STE_DMA_MODE = 0x03u;
	}
}

static int ring_dma_offset(void)
{
	if (!g_pool) {
		return -1;
	}
	unsigned long const base = (unsigned long)g_pool;
	unsigned long const h = (unsigned long)*STE_DMA_CNT_H;
	unsigned long const m = (unsigned long)*STE_DMA_CNT_M;
	unsigned long const l = (unsigned long)*STE_DMA_CNT_L;
	unsigned long const cnt = (h << 16) | (m << 8) | l;
	if (cnt < base || cnt >= base + (unsigned long)AUDIO_DMA_RING_BYTES) {
		return -1;
	}
	return (int)(cnt - base);
}

static unsigned dma_digi_consumer_pos(DigiRing* r)
{
	int const off = ring_dma_offset();
	if (off < 0) {
		return r ? r->last_consumer : 0;
	}
	return (unsigned)off;
}

static void dma_digi_arm(DigiRing* r)
{
	if (r && r->base) {
		dma_arm_loop(r->base, r->size);
	}
}

static void dma_digi_stop_op(DigiRing* r)
{
	(void)r;
	dma_stop();
}

static DigiRingOps const g_dma_digi_ops = {dma_digi_consumer_pos, dma_digi_arm, dma_digi_stop_op};

static DigiInfo const* dma_digi_info(void)
{
	return &g_info;
}

static unsigned dma_digi_capacity(unsigned rate_flags)
{
	unsigned freeb;
	if (!g_ring.base) {
		return 0;
	}
	freeb = digi_ring_free_bytes(&g_ring);
	(void)rate_flags;
	return freeb;
}

static void const* dma_digi_submit(void const* start, void const* end, unsigned rate_flags)
{
	unsigned char const* s = (unsigned char const*)start;
	unsigned char const* e = (unsigned char const*)end;
	unsigned n;
	unsigned written;
	(void)rate_flags;
	if (!g_ring.base || !s || e <= s) {
		return start;
	}
	n = (unsigned)(e - s);
	written = digi_ring_write_available(&g_ring, s, n);
	return s + written;
}

static void dma_digi_pause(void)
{
	dma_stop();
	g_ring.armed = 0;
}

static void dma_digi_resume(void)
{
}

static void dma_digi_flush(void)
{
	dma_digi_pause();
	digi_ring_reset(&g_ring);
	digi_ring_silence(&g_ring, 0);
}

static int dma_digi_active(void)
{
	return g_inited && g_ring.armed;
}

static void dma_install_hooks(void)
{
	g_info.device_rate_hz =
	    (g_rate_idx == (unsigned char)DMA_HW_RATE_12517_IDX) ? 12517u : 25033u;
	g_info.ring_samples = (unsigned)AUDIO_DMA_RING_BYTES;
	Digi_Info = dma_digi_info;
	Digi_Submit = dma_digi_submit;
	Digi_Capacity = dma_digi_capacity;
	Digi_Active = dma_digi_active;
	Digi_Pause = dma_digi_pause;
	Digi_Resume = dma_digi_resume;
	Digi_Flush = dma_digi_flush;
	Digi_Shutdown = Audio_Dma_Shutdown;
}

/*
 * 12517 Hz (rr=01) on STE when _SND bit 1 is set. Fall back to 25033 Hz otherwise.
 */
static int dma_12500_supported(void)
{
	long snd = 0;

	if (Getcookie(C__SND, &snd) == C_FOUND && (snd & 2L) == 0L) {
		return 0;
	}
	return 1;
}

void Audio_Dma_Save_Tos_Sound(void)
{
	if (g_tos_sound_saved) {
		return;
	}
	g_tos_sound_saved = 1;
	/* Same test as the restore below: a plain ST is in neither class and has
	 * no register at $FF8921 to read. */
	if (ST_Hw_Is_Ste_Sound_Class()) {
		g_tos_sound_mode = *STE_DMA_SOUND_MODE;
	}
}

void Audio_Dma_Restore_Tos_Sound(void)
{
	if (!g_tos_sound_saved) {
		return;
	}
	g_tos_sound_saved = 0;
	if (ST_Hw_Is_Falcon_Class()) {
		/* The codec clock is what made later TOS sounds dull; DMAPLAY -> DAC
		 * is the routing TOS uses anyway. */
		(void)Devconnect(DMAPLAY, DAC, CLK25M, CLK50K, NO_SHAKE);
	} else if (ST_Hw_Is_Ste_Sound_Class()) {
		*STE_DMA_SOUND_MODE = g_tos_sound_mode;
	}
}

void Audio_Dma_Connect_Output(void)
{
	dma_stop();
	dma_falcon_matrix_connect();
	dma_mixer_connect();
}

int Audio_Dma_Init(void)
{
	if (g_inited) {
		Audio_Dma_Shutdown();
	}
	if (!ST_Hw_Dma_Audio_Available()) {
		return 0;
	}

	g_rate_idx = dma_12500_supported() ? (unsigned char)DMA_HW_RATE_12517_IDX
	                                   : (unsigned char)DMA_HW_RATE_25033_IDX;

	g_pool = (unsigned char*)Stram_Alloc((unsigned long)AUDIO_DMA_RING_BYTES);
	if (!g_pool) {
		return 0;
	}
	memset(g_pool, 0, (size_t)AUDIO_DMA_RING_BYTES);
	digi_ring_init(&g_ring, g_pool, (unsigned)AUDIO_DMA_RING_BYTES, &g_dma_digi_ops);

	g_inited = 1;
	dma_install_hooks();
	dma_stop();
	dma_falcon_matrix_connect();
	dma_mixer_connect();
	return 1;
}

void Audio_Dma_Shutdown(void)
{
	if (g_inited) {
		dma_digi_flush();
	}
	if (g_pool) {
		Stram_Free(g_pool);
		g_pool = 0;
	}
	g_ring.base = 0;
	g_inited = 0;
	Digi_Clear_Hooks();
}

int Audio_Dma_Inited(void)
{
	return g_inited;
}

#endif /* ATARI_ST */
