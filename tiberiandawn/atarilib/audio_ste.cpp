/*
 * audio_ste.cpp - Atari STe DMA 8-bit mono PCM output for digitized SFX (.AUD in MIX).
 *
 * Requires supervisor (startup calls Super(0)). Probed at init via _MCH/_SND cookies:
 * any DMA-capable machine (_MCH hw != 0) with _SND bit 1 (STE / TT / Falcon). Plain ST
 * is rejected. Microwire mixer ($8922) is STE-only; Falcon uses Devconnect() routing.
 * DMA sound is programmed at **12517 Hz mono 8-bit** when _SND allows it; otherwise 25033 Hz (rr=10).
 * Assets converted for the 25 kHz path carry STE_AUD_FLAG_DUP2X (~11 kHz doubled); DUP2X is
 * ignored at 12.5 kHz so the same buffers play at the correct pitch without resampling. Each refill
 * pulls up to STE_AUDIO_PULL_BLOCK bytes via a caller-supplied LUT
 * (`SteStreamFormat::pull`), so format conversion and per-voice volume can be fused in the driver.
 * Playback uses one `STE_DMA_RING_SAMPLES` byte ring in ST-RAM; DMA is armed once to loop it.
 *
 * **Lifetime**: `Audio_Init` allocates the DMA ring (ST-RAM), per-voice `SteStreamPcmFormat` /
 * `SteStreamIma99Format` objects, and `g_mix_pull[][]` decode scratch. Streams are rebound with
 * `bind_from_aud()` per play; no `new`/`delete` on the audio hot path.
 *
 * **Servicing**: `ste_audio_service_core` runs from the **TOS VBL queue** (`nvbls` / `_vblqueue`):
 * read the DMA frame counter ($FF8909/B/D), then cyclically decode/mix into the ring from
 * `g_ring_write_pos` up to the current DMA read offset. Pull counts are always even. VBL does not
 * call malloc/free/delete; DMA-off teardown is deferred via `g_pending_voice_shutdown`.
 * `Sound_Callback` polls the same core on the main thread (legacy / Win32); **ATARI_ST** game
 * code omits that polling (`THEME.CPP`) so servicing is VBL-only. ST tests wait on VBL and call
 * `Sound_Maintenance` for deferred teardown. The VBL hook never raises IPL
 * (MFP/IKBD at level 6 must stay serviceable during IMA decode). `Play_Sample` uses brief IPL-5
 * sections only on the main thread (blocks VBL at 4, not IKBD).
 *
 * **Mixing**: Up to **two** simultaneous streams are decoded to signed-linear temps, attenuated by
 * each voice's volume, then summed into the ring (32-bit lanes, no per-sample saturation); a single
 * active voice skips the second pull and add pass.
 *
 * Compression 0 = raw PCM, 99 = Westwood AUD (0xDEAF-framed IMA ADPCM). Stereo is not supported.
 *
 * With `ST_BORDER_PROFILE`, `ste_audio_vbl_proc` uses BORDER_COLOR/BORDER_RESTORE (pen 0 red) for
 * audio service timing; `ste_fill_mixed_region` marks the two-voice mix pass in
 * magenta within that interval.
 */

#include "st_hw_probe.h"
#include "function.h"
#include "ccfile.h"
#include "audio.h"
#include "memflag.h"
#include "ste_aud_constants.h"
#include "ste_stream_format.h"
#include "ste_stream_pcm.h"
#include "ste_stream_ima99.h"
#include "st_border_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/ostruct.h>
#include <mint/sysvars.h>

extern Sample_Type SampleType;
extern SFX_Type SoundType;

void (*Audio_Focus_Loss_Function)(void) = 0;

/* $FF8921 rate bits rr: 01 = 12517 Hz, 10 = 25033 Hz (mono bit 7 set separately). */
enum {
	STE_HW_RATE_12517_IDX = 1,
	STE_HW_RATE_25033_IDX = 2
};

enum SteStreamKind {
	STE_STREAM_NONE = 0,
	STE_STREAM_PCM,
	STE_STREAM_IMA99
};

#if defined(__GNUC__) && (defined(__mc68000__) || defined(__M68K__) || defined(__m68k__))
/*
 * Set IPL for short main-thread critical sections (voice bookkeeping). Never call from VBL:
 * IPL 7 would block IKBD; even IPL 6 would block same-priority MFP sources.
 */
static inline unsigned short ste_sr_lock_ipl5(void)
{
	unsigned short t;
	__asm__ __volatile__(
	    "move.w %%sr,%0\n\t"
	    "move.w %0,%%d1\n\t"
	    "andi.w #0xF8FF,%%d1\n\t"
	    "ori.w #0x0500,%%d1\n\t"
	    "move.w %%d1,%%sr"
	    : "=d"(t)
	    :
	    : "d1", "cc", "memory");
	return t;
}
static inline void ste_sr_restore(unsigned short t)
{
	__asm__ __volatile__("move.w %0,%%sr" : : "d"(t) : "cc", "memory");
}
#else
static inline unsigned short ste_sr_lock_ipl5(void)
{
	return 0;
}
static inline void ste_sr_restore(unsigned short) {}
#endif

static volatile unsigned char* const STE_DMA_CTRL = (volatile unsigned char*)0xFF8900UL;
static volatile unsigned char* const STE_DMA_MODE = (volatile unsigned char*)0xFF8901UL;
/* Sound mode / sample frequency (Hatari dmaSnd.c): bits 0–1 = rate index, bit 7 = mono (8-bit mono buffer). */
static volatile unsigned char* const STE_DMA_SOUND_MODE = (volatile unsigned char*)0xFF8921UL;
enum { STE_DMA_SND_MODE_MONO = 0x80u };
static volatile unsigned char* const STE_DMA_START_H = (volatile unsigned char*)0xFF8903UL;
static volatile unsigned char* const STE_DMA_START_M = (volatile unsigned char*)0xFF8905UL;
static volatile unsigned char* const STE_DMA_START_L = (volatile unsigned char*)0xFF8907UL;
/* $FF8909/B/D is the READ-ONLY frame address counter (current DMA fetch position). */
static volatile unsigned char const* const STE_DMA_CNT_H = (volatile unsigned char*)0xFF8909UL;
static volatile unsigned char const* const STE_DMA_CNT_M = (volatile unsigned char*)0xFF890BUL;
static volatile unsigned char const* const STE_DMA_CNT_L = (volatile unsigned char*)0xFF890DUL;
/* Writable frame end address -- writes to the counter at $FF8909/B/D are silently dropped. */
static volatile unsigned char* const STE_DMA_END_H = (volatile unsigned char*)0xFF890FUL;
static volatile unsigned char* const STE_DMA_END_M = (volatile unsigned char*)0xFF8911UL;
static volatile unsigned char* const STE_DMA_END_L = (volatile unsigned char*)0xFF8913UL;

/* STE DMA mixer data ($FFFF8922): many demos poke a small constant; no Microwire framing here. */
static volatile unsigned char* const STE_DMA_MIXER = (volatile unsigned char*)0xFFFF8922UL;

static int g_ste_dma_ok;
static unsigned char g_ste_dma_rate_idx;
int g_ste_pcm_dup2x = 1;

static void const* g_voice_src[STE_MIX_VOICES];
/* Which voice index owns malloc'd g_stream_file_buf (-1 if none). */
static int g_stream_file_voice = -1;

/* Loaded by File_Stream_Sample_Vol for theme scores (THEME.CPP); one buffer at a time. */
static unsigned char* g_stream_file_buf;
static unsigned long g_stream_file_len;

static int g_audio_vbl_slot = -1;
extern "C" void ste_audio_vbl_proc(void);
/*
 * Play_Sample cold-start arms DMA after ste_dma_stop while voices are already marked active.
 * Without this, a VBL between stop and arm sees DMA off and would tear down streams.
 */
static volatile int g_ste_suppress_dma_off_cleanup;

static void* ste_stram_alloc(unsigned long nbytes)
{
	return Stram_Alloc(nbytes);
}

static void ste_stram_free(void* p)
{
	Stram_Free(p);
}

struct SteStreamState {
	int active;
	int play_priority;
	int volume;
	unsigned char vol_lut[256];
	SteStreamKind kind;
	SteStreamFormat* format;
};

static struct SteStreamState g_voice_ss[STE_MIX_VOICES];
static SteStreamPcmFormat g_voice_pcm[STE_MIX_VOICES];
static SteStreamIma99Format g_voice_ima[STE_MIX_VOICES];
static unsigned char g_mix_pull[STE_MIX_VOICES][STE_AUDIO_PULL_BLOCK];
static unsigned char* g_dma_pool;
static volatile int g_pending_voice_shutdown;
/*
 * Ring streaming state (VBL / Sound_Callback after Play_Sample cold arm).
 *   g_ring_write_pos:        next byte offset to mix into (0 .. STE_DMA_RING_SAMPLES-1).
 *   g_stream_samples_written: total samples committed to the ring (EOF tracking / diagnostics).
 */
static unsigned g_ring_write_pos;
static unsigned long g_stream_samples_written;

static int ste_ring_dma_offset(void);

static unsigned short read_le16(unsigned char const* p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static unsigned long read_le32(unsigned char const* p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16)
	       | ((unsigned long)p[3] << 24);
}

static void write_le16(unsigned char* p, unsigned short v)
{
	p[0] = (unsigned char)(v & 0xFFU);
	p[1] = (unsigned char)((v >> 8) & 0xFFU);
}

static void write_le32(unsigned char* p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xFFUL);
	p[1] = (unsigned char)((v >> 8) & 0xFFUL);
	p[2] = (unsigned char)((v >> 16) & 0xFFUL);
	p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

/*
 * 12517 Hz (rr=01) on STE when _SND bit 1 is set. Fall back to 25033 Hz otherwise.
 */
static int ste_dma_12500_supported(void)
{
	long snd = 0;

	if (Getcookie(C__SND, &snd) == C_FOUND && (snd & 2L) == 0L) {
		return 0;
	}
	return 1;
}

static void ste_dma_mixer_connect(void)
{
	if (!g_ste_dma_ok || !ST_Hw_Is_Ste_Class()) {
		return;
	}
	*STE_DMA_MIXER = 0x03;
}

static void ste_falcon_dma_matrix_connect(void)
{
	if (!g_ste_dma_ok || !ST_Hw_Is_Falcon_Class()) {
		return;
	}
	(void)Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);
}

/*
 * TOS sound state, captured before the first change and put back by Sound_End.
 * Devconnect rewires the Falcon connection matrix and clocks the codec for our
 * sample rate; the DAC reconstruction filter follows that clock, so leaving it
 * set makes every later TOS sound - the keyboard click above all - muffled.
 */
static unsigned char g_tos_sound_mode;
static int g_tos_sound_saved;

static void ste_audio_capture_tos_sound(void)
{
	if (g_tos_sound_saved) {
		return;
	}
	g_tos_sound_saved = 1;
	/* Same test as the restore below: a plain ST is neither class and has no
	 * register at $FF8921 to read. */
	if (ST_Hw_Is_Ste_Class()) {
		g_tos_sound_mode = *STE_DMA_SOUND_MODE;
	}
}

static void ste_audio_restore_tos_sound(void)
{
	if (!g_tos_sound_saved) {
		return;
	}
	g_tos_sound_saved = 0;
	if (ST_Hw_Is_Falcon_Class()) {
		/* The codec clock is what made later TOS sounds dull; DMAPLAY -> DAC
		 * is the routing TOS uses anyway. */
		(void)Devconnect(DMAPLAY, DAC, CLK25M, CLK50K, NO_SHAKE);
	} else if (ST_Hw_Is_Ste_Class()) {
		*STE_DMA_SOUND_MODE = g_tos_sound_mode;
	}
}

static void ste_dma_set_address(volatile unsigned char* high_reg, unsigned long phys)
{
	high_reg[0] = (unsigned char)((phys >> 16) & 0xFFU);
	high_reg[2] = (unsigned char)((phys >> 8) & 0xFFU);
	high_reg[4] = (unsigned char)(phys & 0xFFU);
}

static void ste_dma_stop(void)
{
	if (!g_ste_dma_ok) {
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

static int ste_audio_alloc_init(void)
{
	if (g_dma_pool) {
		return 1;
	}
	g_dma_pool = (unsigned char*)ste_stram_alloc((unsigned long)STE_DMA_RING_SAMPLES);
	return g_dma_pool != 0;
}

static void ste_audio_alloc_shutdown(void)
{
	ste_stram_free(g_dma_pool);
	g_dma_pool = 0;
}

/*
 * Global attenuation on every voice (theme, EVA, SFX) before per-sample LUT mapping.
 * Halves requested volume so dual-voice sums stay inside signed 8-bit after ste_mix_two_add.
 */
enum { STE_GAME_VOLUME_NUM = 1, STE_GAME_VOLUME_DEN = 2 };

/* Map a stream's logical 8-bit domain to scaled signed-DMA bytes. */
static void ste_volume_lut_build(unsigned char lut[256], int vol, SteStreamSampleDomain domain)
{
	vol = Bound(vol, 0, 0xFF);
	vol = (vol * STE_GAME_VOLUME_NUM) / STE_GAME_VOLUME_DEN;
	for (unsigned i = 0; i < 256U; ++i) {
		int const s = domain == STE_STREAM_DOMAIN_U8 ? (int)i - 128 : (int)(signed char)(unsigned char)i;
		int o = (s * vol) >> 8;
		if (o > 127) {
			o = 127;
		} else if (o < -128) {
			o = -128;
		}
		lut[i] = (unsigned char)(signed char)o;
	}
}

static void ste_stream_shutdown_one(struct SteStreamState* ss);
static void ste_voice_release_file_heap(int vi);

static void ste_voice_pull_padded(struct SteStreamState* ss, unsigned char* dst, unsigned nsamp)
{
	if (!ss->format || nsamp == 0) {
		memset(dst, 0, (size_t)nsamp);
		return;
	}
	unsigned long const got = ss->format->pull(dst, (unsigned long)nsamp, ss->vol_lut);
	if (got < (unsigned long)nsamp) {
		memset(dst + got, 0, (size_t)(nsamp - (unsigned)got));
	}
	if (got == 0UL) {
		ste_stream_shutdown_one(ss);
	}
}

static void ste_stream_shutdown_one(struct SteStreamState* ss)
{
	if (ss->format) {
		ss->format->reset();
	}
	ss->kind = STE_STREAM_NONE;
	ss->format = 0;
	ss->active = 0;
	ss->play_priority = 0;
}

static void ste_shutdown_all_voices_core(int release_file_buf)
{
	g_ste_suppress_dma_off_cleanup = 0;
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (release_file_buf) {
			ste_voice_release_file_heap(vi);
		}
		ste_stream_shutdown_one(&g_voice_ss[vi]);
		g_voice_src[vi] = 0;
	}
}

static void ste_process_pending_voice_shutdown(void)
{
	if (!g_pending_voice_shutdown) {
		return;
	}
	g_pending_voice_shutdown = 0;
	ste_shutdown_all_voices_core(1);
}

/*
 * Open stream: bind pre-allocated PCM or IMA instance for this voice (no heap).
 */
static int ste_stream_open(struct SteStreamState* ss, int vi, unsigned char const* b, unsigned long aud_bytes, int volume)
{
	ste_stream_shutdown_one(ss);
	if (vi < 0 || vi >= STE_MIX_VOICES || aud_bytes < (unsigned long)STE_AUD_HDR_LEN) {
		return 0;
	}

	SteStreamFormat* f = 0;
	SteStreamKind kind = STE_STREAM_NONE;
	switch (b[11]) {
	case STE_AUD_COMP_PCM:
		if (g_voice_pcm[vi].bind_from_aud(b, aud_bytes)) {
			f = &g_voice_pcm[vi];
			kind = STE_STREAM_PCM;
		}
		break;
	case STE_AUD_COMP_IMA99:
		if (g_voice_ima[vi].bind_from_aud(b, aud_bytes)) {
			f = &g_voice_ima[vi];
			kind = STE_STREAM_IMA99;
		}
		break;
	default:
		break;
	}
	if (!f || f->total_output_samples() == 0UL) {
		if (f) {
			f->reset();
		}
		return 0;
	}
	ss->active = 1;
	ss->kind = kind;
	ss->format = f;
	ss->volume = Bound(volume, 0, 0xFF);
	ste_volume_lut_build(ss->vol_lut, ss->volume, f->sample_domain());
	return 1;
}

/* Arm DMA once: loop `len` bytes at `first` in ST-RAM (start/end not rewritten during play). */
static void ste_dma_arm_loop(unsigned char const* first, unsigned len)
{
	unsigned char const mode = (unsigned char)(STE_DMA_SND_MODE_MONO | g_ste_dma_rate_idx);

	ste_dma_stop();
	ste_dma_mixer_connect();
	if (ST_Hw_Is_Falcon_Class()) {
		*STE_DMA_SOUND_MODE =
		    (unsigned char)((*STE_DMA_SOUND_MODE & (unsigned char)~0x87u) | mode);
	} else {
		*STE_DMA_SOUND_MODE = mode;
	}
	unsigned long const s = (unsigned long)first;
	unsigned long const e = s + (unsigned long)len;
	ste_dma_set_address(STE_DMA_START_H, s);
	ste_dma_set_address(STE_DMA_END_H, e);
	/* $FF8901 bits 0+1: %11 = play with loop (auto-reload start/end at end-of-sweep). */
	if (ST_Hw_Is_Falcon_Class()) {
		*STE_DMA_MODE = (unsigned char)(*STE_DMA_MODE | 0x03u);
		*STE_DMA_CTRL = (unsigned char)(*STE_DMA_CTRL | 0x03u);
	} else {
		*STE_DMA_MODE = 0x03u;
	}
}

/* Byte offset of the current DMA fetch in g_dma_pool, or -1 if the counter is outside the ring. */
static int ste_ring_dma_offset(void)
{
	if (!g_dma_pool) {
		return -1;
	}
	unsigned long const base = (unsigned long)g_dma_pool;
	unsigned long const h = (unsigned long)*STE_DMA_CNT_H;
	unsigned long const m = (unsigned long)*STE_DMA_CNT_M;
	unsigned long const l = (unsigned long)*STE_DMA_CNT_L;
	unsigned long const cnt = (h << 16) | (m << 8) | l;
	if (cnt < base || cnt >= base + (unsigned long)STE_DMA_RING_SAMPLES) {
		return -1;
	}
	return (int)(cnt - base);
}

static int ste_any_voice_active(void)
{
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_ss[vi].active) {
			return 1;
		}
	}
	return 0;
}

static void ste_voice_release_file_heap(int vi)
{
	if (g_stream_file_voice == vi && g_stream_file_buf) {
		free(g_stream_file_buf);
		g_stream_file_buf = 0;
		g_stream_file_len = 0;
		g_stream_file_voice = -1;
	}
}

/*
 * Sum two signed-mono voice scratch buffers into `dst` (`nsamp` bytes, even). The bulk path adds
 * 32-bit lanes with plain integer addition (carry may spill between adjacent bytes). Odd ring write
 * offsets mix the leading/trailing byte scalar; a 2-byte tail uses one 16-bit add.
 */
static void ste_mix_two_add(unsigned char* dst, unsigned char const* a, unsigned char const* b, unsigned nsamp)
{
	unsigned i = 0;

	if (((unsigned long)dst & 1UL) != 0UL) {
		int const s = (int)(signed char)a[0] + (int)(signed char)b[0];
		dst[0] = (unsigned char)(signed char)s;
		i = 1U;
	}

	while (i + 4U <= nsamp) {
		*(unsigned long*)(dst + i) = *(unsigned long const*)(a + i) + *(unsigned long const*)(b + i);
		i += 4U;
	}

	if (i + 2U <= nsamp) {
		*(unsigned short*)(dst + i) = *(unsigned short const*)(a + i) + *(unsigned short const*)(b + i);
		i += 2U;
	}

	if (i < nsamp) {
		int const s = (int)(signed char)a[i] + (int)(signed char)b[i];
		dst[i] = (unsigned char)(signed char)s;
	}
}

static void ste_fill_mixed_region(unsigned char* dst, unsigned nsamp)
{
	if (nsamp == 0 || (nsamp & 1U) != 0U) {
		return;
	}
	
	/* Array holding the indices of currently active voices (up to STE_MIX_VOICES).
	   Used to reference which audio streams will be mixed in this fill. */
	int vidx[STE_MIX_VOICES];
	int nactive = 0;
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_ss[vi].active) {
			vidx[nactive++] = vi;
		}
	}
	if (nactive == 0) {
		memset(dst, 0, (size_t)nsamp);
		return;
	}
	if (nactive == 1) {
		int const vi = vidx[0];
		unsigned n = nsamp <= (unsigned)STE_AUDIO_PULL_BLOCK ? nsamp : (unsigned)STE_AUDIO_PULL_BLOCK;
		ste_voice_pull_padded(&g_voice_ss[vi], dst, n);
		return;
	}
	unsigned n = nsamp <= (unsigned)STE_AUDIO_PULL_BLOCK ? nsamp : (unsigned)STE_AUDIO_PULL_BLOCK;
	int const vi0 = vidx[0];
	int const vi1 = vidx[1];
	ste_voice_pull_padded(&g_voice_ss[vi0], g_mix_pull[vi0], n);
	BORDER_COLOR(0x0704u);
	ste_voice_pull_padded(&g_voice_ss[vi1], g_mix_pull[vi1], n);
	BORDER_COLOR_SET(0x0707u);
	ste_mix_two_add(dst, g_mix_pull[vi0], g_mix_pull[vi1], n);
	BORDER_RESTORE();
}

/* Mix `nbytes` (even) into the ring at `ring_off`, wrapping at STE_DMA_RING_SAMPLES. */
static void ste_ring_write_mixed(unsigned ring_off, unsigned nbytes)
{
	unsigned filled = 0;
	while (filled < nbytes) {
		unsigned batch = nbytes - filled;
		if (batch > (unsigned)STE_AUDIO_PULL_BLOCK) {
			batch = (unsigned)STE_AUDIO_PULL_BLOCK;
		}
		/* Each fill is one contiguous slice; never let ring_off + batch pass the pool end. */
		unsigned const to_end = (unsigned)STE_DMA_RING_SAMPLES - ring_off;
		if (batch > to_end) {
			batch = to_end;
		}
		batch &= ~1U;
		if (batch == 0) {
			ring_off = 0;
			continue;
		}
		ste_fill_mixed_region(g_dma_pool + ring_off, batch);
		ring_off = (ring_off + batch) % (unsigned)STE_DMA_RING_SAMPLES;
		filled += batch;
	}
	g_stream_samples_written += (unsigned long)nbytes;
}

static void (**ste_vbl_queue_table(void))(void)
{
	return (void (**)(void))*(unsigned long*)0x456UL;
}

static void ste_audio_vbl_install(void)
{
	if (g_audio_vbl_slot >= 0) {
		return;
	}
	short const n = *nvbls;
	if (n <= 0) {
		return;
	}
	void (**vq)(void) = ste_vbl_queue_table();
	int first_free_found = 0;
	for (int i = 0; i < n; ++i) {
		if (vq[i] == 0) {
			if (!first_free_found) {
				// There is a known bug where the first entry in the table gets overwritten when GEM is initialized.
				// So, skip this free slot and use the next one.
				first_free_found = 1;
				continue;
			}
			vq[i] = ste_audio_vbl_proc;
			g_audio_vbl_slot = i;
			return;
		}
	}
	g_audio_vbl_slot = -1;
}

static void ste_audio_vbl_remove(void)
{
	if (g_audio_vbl_slot < 0) {
		return;
	}
	short const n = *nvbls;
	void (**vq)(void) = ste_vbl_queue_table();
	if (g_audio_vbl_slot < n) {
		vq[g_audio_vbl_slot] = 0;
	}
	g_audio_vbl_slot = -1;
}

static void ste_audio_service_core(void)
{
	if (!g_ste_dma_ok || !ste_any_voice_active()) {
		return;
	}
	// If DMA playback is off, clean up voices and streams unless cleanup is suppressed, then exit.
	if ((*STE_DMA_MODE & 0x01u) == 0u) {
		// happens during sample transition or audio shutdown to skip cleanup
		if (g_ste_suppress_dma_off_cleanup) {
			return;
		}
		ste_dma_stop();
		g_pending_voice_shutdown = 1;
		g_ste_suppress_dma_off_cleanup = 0;
		return;
	}

	int const dma_off = ste_ring_dma_offset();
	if (dma_off < 0) {
		return;
	}

	unsigned const w = g_ring_write_pos;
	/* Ring span from the write cursor forward to the DMA read pointer (already-consumed region). */
	unsigned to_fill =
	    ((unsigned)dma_off + (unsigned)STE_DMA_RING_SAMPLES - w) % (unsigned)STE_DMA_RING_SAMPLES;
	to_fill &= ~1U;
	if (to_fill == 0) {
		return;
	}

	if (!ste_any_voice_active()) {
		ste_dma_stop();
		return;
	}

	ste_ring_write_mixed(w, to_fill);
	g_ring_write_pos = (w + to_fill) % (unsigned)STE_DMA_RING_SAMPLES;

	if (!ste_any_voice_active()) {
		ste_dma_stop();
	}
}

extern "C" void ste_audio_vbl_proc(void)
{
	/*
	 * Never read or modify SR in this hook. TOS dispatches VBL at IPL 4; IKBD (MFP 6) must
	 * preempt during decode/mix. No malloc/free/delete — voice teardown is deferred to main.
	 */
	BORDER_COLOR(0x0700u);
	ste_audio_service_core();
	BORDER_RESTORE();
}

void Sound_Maintenance(void)
{
	ste_process_pending_voice_shutdown();
}

void Sound_Callback(void)
{
	Sound_Maintenance();
	ste_audio_service_core();
}

/*
 * Pick a voice for a new stream.
 *
 * Priority matches DOS soundio: higher numeric value = stronger.
 * Preempt only an active voice whose priority is less than or equal to the incoming value
 * (same rule as Get_Free_Sample_Handle: skip while existing.Priority > incoming).
 *
 * Returns -1 if both voices are active with stronger priority than incoming.
 */
static int ste_pick_voice_for_play(int priority)
{
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (!g_voice_ss[vi].active) {
			return vi;
		}
	}
	for (int vi = STE_MIX_VOICES - 1; vi >= 0; --vi) {
		if (g_voice_ss[vi].play_priority <= priority) {
			return vi;
		}
	}
	return -1;
}

int File_Stream_Sample(char const* filename, BOOL real_time_start)
{
	return File_Stream_Sample_Vol(filename, 0xFF, real_time_start);
}

/*
 * Theme scores (THEME.CPP) call this for "*.AUD" on disk. Win32 streamed from file;
 * here we load the whole file then play through the same STE path as SFX.
 */
static void ste_shutdown_all_voices(void)
{
	g_pending_voice_shutdown = 0;
	ste_shutdown_all_voices_core(1);
}

int File_Stream_Sample_Vol(char const* filename, int volume, BOOL)
{
	if (!g_ste_dma_ok || !filename) {
		return -1;
	}
	ste_process_pending_voice_shutdown();
	unsigned short sr = ste_sr_lock_ipl5();
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_src[vi] == (void const*)g_stream_file_buf) {
			ste_voice_release_file_heap(vi);
			ste_stream_shutdown_one(&g_voice_ss[vi]);
			g_voice_src[vi] = 0;
		}
	}
	if (!ste_any_voice_active()) {
		ste_dma_stop();
	}
	ste_sr_restore(sr);
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;

	CCFileClass file(filename);
	if (!file.Is_Available()) {
		return -1;
	}
	long const sz = file.Size();
	if (sz < 12 || sz > (long)(STE_AUD99_MAX_COMPRESSED_PAYLOAD + (long)STE_AUD_HDR_LEN)) {
		return -1;
	}
	unsigned char* buf = (unsigned char*)malloc((unsigned long)sz);
	if (!buf) {
		return -1;
	}
	if (file.Read(buf, sz) != sz) {
		free(buf);
		return -1;
	}
	g_stream_file_buf = buf;
	g_stream_file_len = (unsigned long)sz;
	if (Play_Sample(buf, PRIORITY_MAX, volume, 0) < 0) {
		free(g_stream_file_buf);
		g_stream_file_buf = 0;
		g_stream_file_len = 0;
		return -1;
	}
	return 1;
}

void* Load_Sample(char const*) { return NULL; }
long Load_Sample_Into_Buffer(char const*, void*, long) { return 0; }
long Sample_Read(int, void*, long) { return 0; }
void Free_Sample(void const*) {}

void Sample_Make_PCM(void* sample)
{
	if (!sample) {
		return;
	}

	unsigned char* const aud = (unsigned char*)sample;
	if (aud[11] != STE_AUD_COMP_IMA99) {
		return;
	}

	unsigned long const payload_bytes = read_le32(aud + 2);
	unsigned long const uncomp = read_le32(aud + 6);
	if (payload_bytes == 0UL || payload_bytes > STE_AUD99_MAX_COMPRESSED_PAYLOAD || uncomp == 0UL
	    || (uncomp & 1UL) != 0UL || uncomp > STE_AUD99_MAX_DECODED_PCM_BYTES || (aud[10] & AUD_FLAG_STEREO) != 0) {
		return;
	}

	unsigned long const aud_bytes = (unsigned long)STE_AUD_HDR_LEN + payload_bytes;
	SteStreamIma99Format probe;
	if (!probe.bind_from_aud(aud, aud_bytes)) {
		return;
	}

	unsigned long const total_samples = probe.total_output_samples();
	/*
	 * Some Westwood 16-bit IMA assets decode to an odd sample count. The decode/skip path only
	 * services even-sized pulls, so drop a single trailing sample when rewriting to packed PCM.
	 */
	unsigned long const convert_samples = total_samples & ~1UL;
	if (convert_samples == 0UL || probe.skip(convert_samples) != convert_samples) {
		return;
	}

	SteStreamIma99Format convert;
	if (!convert.bind_from_aud(aud, aud_bytes)) {
		return;
	}

	unsigned char identity_lut[256];
	for (unsigned i = 0; i < 256U; ++i) {
		identity_lut[i] = (unsigned char)i;
	}

	unsigned char scratch[STE_AUDIO_PULL_BLOCK];
	unsigned char* const payload = aud + STE_AUD_HDR_LEN;
	unsigned long left = convert_samples;
	unsigned long phase = 0UL;
	unsigned long written = 0UL;

	while (left > 0UL) {
		unsigned long batch = left > (unsigned long)STE_AUDIO_PULL_BLOCK ? (unsigned long)STE_AUDIO_PULL_BLOCK : left;
		batch &= ~1UL;
		if (batch == 0UL) {
			return;
		}

		unsigned long const got = convert.pull(scratch, batch, identity_lut);
		if (got != batch) {
			return;
		}

		for (unsigned long i = 0; i < got; ++i) {
			if ((phase & 1UL) == 0UL) {
				payload[written++] = (unsigned char)((int)(signed char)scratch[i] + 128);
			}
			++phase;
		}
		left -= got;
	}

	unsigned short rate = read_le16(aud);
	if (rate > 1U) {
		rate = (unsigned short)(rate / 2U);
	}

	write_le16(aud, rate);
	write_le32(aud + 2, written);
	write_le32(aud + 6, written);
	aud[10] = STE_AUD_FLAG_DUP2X;
	aud[11] = STE_AUD_COMP_PCM;
}

BOOL Audio_Init(HWND, int bits_per_sample, BOOL stereo, int rate, int)
{
	ste_audio_capture_tos_sound();
	ste_process_pending_voice_shutdown();
	(void)stereo;
	(void)rate;
	/* Request 8 from startup; 16 is harmless (sources may still be 16-bit in .AUD flags). */
	(void)bits_per_sample;
	ste_audio_vbl_remove();
	g_ste_dma_ok = ST_Hw_Dma_Audio_Available() ? 1 : 0;
	if (g_ste_dma_ok && ste_dma_12500_supported()) {
		g_ste_dma_rate_idx = (unsigned char)STE_HW_RATE_12517_IDX;
		g_ste_pcm_dup2x = 0;
	} else {
		g_ste_dma_rate_idx = (unsigned char)STE_HW_RATE_25033_IDX;
		g_ste_pcm_dup2x = 1;
	}
	g_pending_voice_shutdown = 0;
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
	ste_audio_alloc_shutdown();
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;
	Audio_Focus_Loss_Function = 0;
	if (!g_ste_dma_ok) {
		long mch = 0;
		long snd = 0;
		(void)Getcookie(C__MCH, &mch);
		(void)Getcookie(C__SND, &snd);
		printf("STE-DMA: Audio_Init failed (_MCH=$%lX _SND=$%lX; need _MCH hw != 0, _SND bit 1)\n",
		    (unsigned long)mch, (unsigned long)snd);
		fflush(stdout);
		SampleType = SAMPLE_NONE;
		SoundType = SFX_NONE;
		return FALSE;
	}
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		g_voice_pcm[vi].reset();
		g_voice_ima[vi].reset();
	}
	if (!ste_audio_alloc_init()) {
		printf("STE-DMA: Audio_Init failed (DMA ring ST-RAM)\n");
		fflush(stdout);
		SampleType = SAMPLE_NONE;
		SoundType = SFX_NONE;
		return FALSE;
	}
	SampleType = SAMPLE_SB;
	SoundType = SFX_DMA_SOUND;
	ste_dma_stop();
	ste_falcon_dma_matrix_connect();
	ste_dma_mixer_connect();
	ste_audio_vbl_install();
	DBG_INFO("STE-DMA: Audio_Init OK (%u Hz mono, dup2x=%d, hw=%d)",
	    g_ste_dma_rate_idx == STE_HW_RATE_12517_IDX ? 12517u : 25033u,
	    g_ste_pcm_dup2x,
	    ST_Hw_Machine_Major());
	return TRUE;
}

void Sound_End(void)
{
	ste_audio_vbl_remove();
	ste_dma_stop();
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
	ste_audio_alloc_shutdown();
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;
	ste_audio_restore_tos_sound();
}

void Stop_Sample(int)
{
	ste_dma_stop();
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
}

BOOL Sample_Status(int) { return ste_any_voice_active() ? TRUE : FALSE; }

BOOL Is_Sample_Playing(void const* sample)
{
	if (!sample) {
		return FALSE;
	}
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_ss[vi].active && g_voice_src[vi] == sample) {
			return TRUE;
		}
	}
	return FALSE;
}

void Stop_Sample_Playing(void const* sample)
{
	if (!sample) {
		return;
	}
	ste_process_pending_voice_shutdown();
	unsigned short sr = ste_sr_lock_ipl5();
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_src[vi] == sample) {
			ste_voice_release_file_heap(vi);
			ste_stream_shutdown_one(&g_voice_ss[vi]);
			g_voice_src[vi] = 0;
		}
	}
	if (!ste_any_voice_active()) {
		ste_dma_stop();
	}
	ste_sr_restore(sr);
}

int Play_Sample(void const* sample, int priority, int volume, signed short)
{
	if (!g_ste_dma_ok || !sample) {
		return -1;
	}
	unsigned char const* b = (unsigned char const*)sample;
	unsigned long aud_bytes;
	if (sample == (void const*)g_stream_file_buf && g_stream_file_len >= (unsigned long)STE_AUD_HDR_LEN) {
		aud_bytes = g_stream_file_len;
	} else {
		unsigned long const szf = read_le32(b + 2);
		unsigned long const uncomp = read_le32(b + 6);
		unsigned char const compression = b[11];
		if (szf > STE_AUD99_MAX_COMPRESSED_PAYLOAD) {
			return -1;
		}
		aud_bytes = (unsigned long)STE_AUD_HDR_LEN + szf;
		if (compression == STE_AUD_COMP_PCM && szf == 0 && uncomp > 0 && uncomp <= STE_AUD99_MAX_DECODED_PCM_BYTES) {
			aud_bytes = (unsigned long)STE_AUD_HDR_LEN + uncomp;
		}
	}
	if (!g_dma_pool) {
		return -1;
	}

	ste_process_pending_voice_shutdown();

	int const vi = ste_pick_voice_for_play(priority);
	if (vi < 0) {
		return -1;
	}
	int cold_arm = 0;

	{
		unsigned short const sr = ste_sr_lock_ipl5();
		if (g_voice_ss[vi].active) {
			ste_voice_release_file_heap(vi);
			ste_stream_shutdown_one(&g_voice_ss[vi]);
			g_voice_src[vi] = 0;
		}
		cold_arm = 1;
		for (int i = 0; i < STE_MIX_VOICES; ++i) {
			if (g_voice_ss[i].active) {
				cold_arm = 0;
				break;
			}
		}
		if (cold_arm) {
			g_ste_suppress_dma_off_cleanup = 1;
		}
		ste_sr_restore(sr);
	}

	if (!ste_stream_open(&g_voice_ss[vi], vi, b, aud_bytes, volume)) {
		if (cold_arm) {
			g_ste_suppress_dma_off_cleanup = 0;
		}
		static int s_open_fail_logged;
		if (!s_open_fail_logged) {
			s_open_fail_logged = 1;
			printf(
			    "STE-DMA: ste_stream_open failed (once) - rate=%u file_size=%lu uncomp=%lu flags=%u compression=%u aud_bytes=%lu\n",
			    (unsigned)read_le16(b),
			    read_le32(b + 2),
			    read_le32(b + 6),
			    (unsigned)b[10],
			    (unsigned)b[11],
			    aud_bytes);
			fflush(stdout);
		}
		return -1;
	}
	g_voice_ss[vi].play_priority = priority;
	g_voice_src[vi] = sample;
	if (sample == (void const*)g_stream_file_buf) {
		g_stream_file_voice = vi;
	}

	if (!cold_arm) {
		/*
		 * Overlay voices should start at their own sample start; they join the mix on the
		 * next service pass rather than inheriting elapsed time from an older stream.
		 */
		return 1;
	}

	/* Half-ring prefill, then loop the whole ring from DMA (start/end programmed once). */
	ste_dma_stop();
	memset(g_dma_pool, 0, (size_t)STE_DMA_RING_SAMPLES);
	g_ring_write_pos = 0;
	g_stream_samples_written = 0UL;
	unsigned const prefill = (unsigned)STE_DMA_RING_SAMPLES / 2U;
	ste_ring_write_mixed(0, prefill);
	g_ring_write_pos = prefill % (unsigned)STE_DMA_RING_SAMPLES;
	ste_dma_arm_loop(g_dma_pool, STE_DMA_RING_SAMPLES);
	g_ste_suppress_dma_off_cleanup = 0;
	return 1;
}

int Play_Sample_Handle(void const* sample, int priority, int volume, signed short panloc, int)
{
	return Play_Sample(sample, priority, volume, panloc);
}

int Set_Sound_Vol(int) { return 0; }
int Set_Score_Vol(int) { return 0; }
void Fade_Sample(int handle, int)
{
	/* TODO: replace this temporary Atari behavior with a real per-handle fade. */
	Stop_Sample(handle);
}
int Get_Free_Sample_Handle(int) { return 1; }
int Get_Digi_Handle(void) { return g_ste_dma_ok ? 1 : -1; }

long Sample_Length(void const* sample)
{
	if (!sample) {
		return 0;
	}
	unsigned char const* b = (unsigned char const*)sample;
	if (read_le16(b + 0) == 0) {
		return 0;
	}
	return (long)read_le32(b + 6);
}

void Restore_Sound_Buffers(void) {}

/*
 * Yield STE DMA to STV playback: stop voices, stop DMA, remove audio VBL.
 * Keeps the game DMA ring allocated for Ste_Audio_Reclaim_Dma.
 */
void Ste_Audio_Yield_Dma(void)
{
	ste_process_pending_voice_shutdown();
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
	ste_dma_stop();
	ste_audio_vbl_remove();
}

/*
 * Reclaim STE DMA after STV: reinstall mixer/VBL; idle until Theme/Play_Sample arms.
 */
void Ste_Audio_Reclaim_Dma(void)
{
	if (!g_ste_dma_ok) {
		return;
	}
	ste_dma_stop();
	ste_falcon_dma_matrix_connect();
	ste_dma_mixer_connect();
	ste_audio_vbl_install();
}

BOOL Set_Primary_Buffer_Format(void) { return TRUE; }
BOOL Start_Primary_Sound_Buffer(BOOL) { return TRUE; }
void Stop_Primary_Sound_Buffer(void) { ste_dma_stop(); }
