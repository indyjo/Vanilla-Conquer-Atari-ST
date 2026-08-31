/*
 * audio.cpp - C&C digitized mixer for Atari ST (voices, VBL mix, AUDX, Play_Sample).
 *
 * Hardware output is a Digi_* HAL: Audio_Dma_* (STE-era DMA) or Timer_Dac_* (YM/Covox).
 * `Audio_Init` picks the backend; DUP2X is honoured only when Digi_Info reports ~25 kHz.
 * Each refill pulls up to STE_AUDIO_PULL_BLOCK bytes via a caller-supplied LUT
 * (`SteStreamFormat::pull`), so format conversion and per-voice volume can be fused.
 *
 * **Lifetime**: `Audio_Init` installs a Digi backend (DMA ST-RAM ring or timer soft ring),
 * per-voice `SteStreamPcmFormat` / `SteStreamIma99Format` objects, and `g_mix_pull[][]`
 * decode scratch. Streams are rebound with `bind_from_aud()` per play; no `new`/`delete`
 * on the audio hot path.
 *
 * **Servicing**: `ste_audio_service_core` runs from the **TOS VBL queue** (`nvbls` / `_vblqueue`):
 * query Digi_Capacity, mix into a stage buffer, Digi_Submit into the device ring. Pull counts
 * are always even. VBL does not call malloc/free/delete; DMA-off teardown is deferred via
 * `g_pending_voice_shutdown`.
 * `Sound_Callback` (main thread, via `Theme.AI`) refills AUDX page-pointer rings (GEMDOS OK)
 * and runs `Sound_Maintenance`. Device-ring mix stays on the **VBL** path only — never
 * `AUDX_Pool_Read` / `CCFileClass` from VBL. ST tests should call `Sound_Callback` so rings
 * refill. The VBL hook never raises IPL (MFP/IKBD at level 6 must stay serviceable during
 * IMA decode). `Play_Sample` uses brief IPL-5 sections only on the main thread (blocks VBL
 * at 4, not IKBD).
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
#include "audx/audx.h"
#include "audx/audx_page_cache.h"
#include "audx/ste_stream_memory_source.h"
#include "audx/ste_stream_page_ring_source.h"
#include "ccfile.h"
#include "audio.h"
#include "audio/audio_dma.h"
#include "audio/audio_timer_dac.h"
#include "audio/digi_audio.h"
#include "ste_aud_constants.h"
#include "ste_stream_format.h"
#include "ste_stream_pcm.h"
#include "ste_stream_ima99.h"
#include "st_audio_cfg.h"
#include "st_border_profile.h"
#include "st_digi_movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/ostruct.h>
#include <mint/sysvars.h>

extern Sample_Type SampleType;
extern SFX_Type SoundType;

void (*Audio_Focus_Loss_Function)(void) = 0;

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

static int g_audio_ok; /* DMA or YM/Covox timer DAC */
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
 * Play_Sample cold-start Flushes then prefills while voices are already marked active.
 * Without this, a VBL between Flush and prefill would tear down streams.
 */
static volatile int g_ste_suppress_dma_off_cleanup;

struct SteStreamState {
	int active;
	int play_priority;
	int volume; /* Per-play base 0..255 (Theme passes 0xFF). */
	int is_score; /* Set for File_Stream_Sample_Vol (theme) voices. */
	unsigned char vol_lut[256];
	SteStreamKind kind;
	SteStreamFormat* format;
};

/* Global score scale (Options -> Set_Score_Vol); mirrors LockedData.ScoreVolume. */
static int g_score_volume = 0xFF;

static struct SteStreamState g_voice_ss[STE_MIX_VOICES];
static SteStreamPcmFormat g_voice_pcm[STE_MIX_VOICES];
static SteStreamIma99Format g_voice_ima[STE_MIX_VOICES];
static SteStreamMemorySource g_voice_mem_src[STE_MIX_VOICES];
static SteStreamPageRingSource g_voice_page_src[STE_MIX_VOICES];
static unsigned char g_mix_pull[STE_MIX_VOICES][STE_AUDIO_PULL_BLOCK];
static volatile int g_pending_voice_shutdown;
/*
 * Digi HAL fill path (VBL): Digi_Capacity + Digi_Submit; write cursor lives in the backend ring.
 * g_stream_samples_written: total samples committed (EOF tracking / diagnostics).
 */
static unsigned char g_digi_stage[STE_AUDIO_PULL_BLOCK];
static unsigned long g_stream_samples_written;

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

static void ste_fill_mixed_region(unsigned char* dst, unsigned nsamp);

static void digi_output_stop(void)
{
	if (Digi_Flush) {
		Digi_Flush();
	}
}

static unsigned digi_client_rate_flags(void)
{
	if (Digi_Info && Digi_Info()->device_rate_hz < 10000u) {
		return DIGI_RATE_6250;
	}
	return DIGI_RATE_12500;
}

static void digi_submit_mixed(unsigned nbytes)
{
	unsigned const flags = digi_client_rate_flags();

	nbytes &= ~1U;
	if (nbytes > (unsigned)STE_AUDIO_PULL_BLOCK) {
		nbytes = (unsigned)STE_AUDIO_PULL_BLOCK;
	}
	if (nbytes == 0) {
		return;
	}
	ste_fill_mixed_region(g_digi_stage, nbytes);
	(void)Digi_Submit(g_digi_stage, g_digi_stage + nbytes, flags);
	g_stream_samples_written += (unsigned long)nbytes;
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

/* Score voices: (base * g_score_volume) >> 8, same idea as LockedData.ScoreVolume * st->Volume. */
static int ste_voice_scaled_volume(struct SteStreamState const* ss)
{
	int vol = ss->volume;
	if (ss->is_score) {
		vol = (vol * g_score_volume) >> 8;
	}
	return vol;
}

static void ste_voice_rebuild_vol_lut(struct SteStreamState* ss)
{
	if (!ss->format) {
		return;
	}
	ste_volume_lut_build(ss->vol_lut, ste_voice_scaled_volume(ss), ss->format->sample_domain());
}

/* Tag/untag a voice as score and rebuild its LUT (vanilla SampleTracker::IsScore). */
static void ste_voice_set_score(struct SteStreamState* ss, int is_score)
{
	ss->is_score = is_score ? 1 : 0;
	ste_voice_rebuild_vol_lut(ss);
}

static void ste_stream_shutdown_one(struct SteStreamState* ss);
static void ste_voice_reset_sources(int vi);
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
	/*
	 * pull==0 can be true EOF or a temporary page-ring underrun. Only tear the voice
	 * down on EOF so Theme.AI does not treat a stall as "song finished" and pick another.
	 */
	if (got == 0UL && ss->format->at_end()) {
		int const vi = (int)(ss - &g_voice_ss[0]);
		ste_stream_shutdown_one(ss);
		/* Unpin page-ring slabs here; never free() on VBL (GEMDOS-unsafe). */
		if (vi >= 0 && vi < STE_MIX_VOICES) {
			ste_voice_reset_sources(vi);
			g_voice_src[vi] = 0;
		}
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
	ss->is_score = 0;
}

static void ste_voice_reset_sources(int vi)
{
	if (vi < 0 || vi >= STE_MIX_VOICES) {
		return;
	}
	g_voice_mem_src[vi].reset();
	g_voice_page_src[vi].reset();
}

static void ste_shutdown_all_voices_core(int release_file_buf)
{
	g_ste_suppress_dma_off_cleanup = 0;
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (release_file_buf) {
			ste_voice_release_file_heap(vi);
		}
		ste_stream_shutdown_one(&g_voice_ss[vi]);
		ste_voice_reset_sources(vi);
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
 * AUDX meta uses a page-pointer ring (RankCache <=64 KiB, stream slabs above);
 * legacy in-memory AUD uses memory / IMA sources (no GEMDOS on the pull path).
 */
static int ste_stream_open(struct SteStreamState* ss, int vi, unsigned char const* b, unsigned long aud_bytes, int volume)
{
	ste_stream_shutdown_one(ss);
	g_voice_mem_src[vi].reset();
	g_voice_page_src[vi].reset();
	if (vi < 0 || vi >= STE_MIX_VOICES || !b) {
		return 0;
	}

	SteStreamFormat* f = 0;
	SteStreamKind kind = STE_STREAM_NONE;

	if (AUDX_Is_Meta(b)) {
		AudxPrefix const* pfx = AUDX_As_Prefix(b);
		SteStreamSource* src = 0;
		uint32_t const span = pfx->pool_data_size ? pfx->pool_data_size : pfx->size;
		int const use_stream = (pfx->size > AUDX_PAGE_CACHE_MAX) ? 1 : 0;

		if (pfx->pool_id == 0 || pfx->size == 0 || span == 0) {
			return 0;
		}
		if (!g_voice_page_src[vi].bind(pfx->pool_id, pfx->pool_data_begin, span, use_stream)) {
			return 0;
		}
		src = &g_voice_page_src[vi];
		if (pfx->compression == STE_AUD_COMP_PCM
		    && g_voice_pcm[vi].bind_source(pfx->rate, pfx->flags, pfx->compression, pfx->size, pfx->uncomp, src)) {
			f = &g_voice_pcm[vi];
			kind = STE_STREAM_PCM;
		}
	} else {
		if (aud_bytes < (unsigned long)STE_AUD_HDR_LEN) {
			return 0;
		}
		switch (b[11]) {
		case STE_AUD_COMP_PCM: {
			unsigned long const size = read_le32(b + 2);
			unsigned long const uncomp = read_le32(b + 6);
			unsigned long payload = aud_bytes - (unsigned long)STE_AUD_HDR_LEN;
			if (size > 0UL && size < payload) {
				payload = size;
			}
			if (!g_voice_mem_src[vi].bind(b + STE_AUD_HDR_LEN, payload)) {
				return 0;
			}
			if (g_voice_pcm[vi].bind_source(read_le16(b), b[10], b[11], size, uncomp, &g_voice_mem_src[vi])) {
				f = &g_voice_pcm[vi];
				kind = STE_STREAM_PCM;
			}
			break;
		}
		case STE_AUD_COMP_IMA99:
			if (g_voice_ima[vi].bind_from_aud(b, aud_bytes)) {
				f = &g_voice_ima[vi];
				kind = STE_STREAM_IMA99;
			}
			break;
		default:
			break;
		}
	}
	if (!f || f->total_output_samples() == 0UL) {
		if (f) {
			f->reset();
		}
		g_voice_mem_src[vi].reset();
		g_voice_page_src[vi].reset();
		return 0;
	}
	ss->active = 1;
	ss->kind = kind;
	ss->format = f;
	ss->is_score = 0;
	ss->volume = Bound(volume, 0, 0xFF);
	ste_voice_rebuild_vol_lut(ss);
	return 1;
}

static void ste_page_ring_service_all(void)
{
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_ss[vi].active) {
			g_voice_page_src[vi].service();
		}
	}
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
	/*
	 * VBL is installed only after Audio_Init has the Digi hooks. Yield/Sound_End
	 * remove it before teardown. Play_Sample sets suppress around Flush+prefill
	 * so we do not race the cold arm (Capacity is full while !armed).
	 */
	if (g_ste_suppress_dma_off_cleanup) {
		return;
	}
	if (!ste_any_voice_active()) {
		if (Digi_Active()) {
			digi_output_stop();
			g_pending_voice_shutdown = 1;
		}
		return;
	}

	unsigned to_fill = Digi_Capacity(digi_client_rate_flags()) & ~1U;
	if (to_fill == 0) {
		return;
	}
	digi_submit_mixed(to_fill);
	if (!ste_any_voice_active()) {
		digi_output_stop();
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
	/* GEMDOS-safe page-ring refill on main thread; DMA mix stays on VBL. */
	ste_page_ring_service_all();
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

static void ste_shutdown_all_voices(void)
{
	g_pending_voice_shutdown = 0;
	ste_shutdown_all_voices_core(1);
}

int File_Stream_Sample(char const* filename, BOOL real_time_start)
{
	return File_Stream_Sample_Vol(filename, 0xFF, real_time_start);
}

static int ste_play_sample(void const* sample, int priority, int volume, int as_score);

/*
 * Theme scores (THEME.CPP) call this for "*.AUD". Prefer MIX Retrieve (AUDX meta in
 * cached SOUNDS/SCORES/SPEECH). Uncached AUDX meta may be loaded (28 bytes, main thread).
 * Classic PCM/IMA named files are not streamed — no CCFile/GEMDOS on the VBL pull path.
 */
int File_Stream_Sample_Vol(char const* filename, int volume, BOOL)
{
	if (!g_audio_ok || !filename) {
		return -1;
	}
	ste_process_pending_voice_shutdown();
	unsigned short sr = ste_sr_lock_ipl5();
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		if (g_voice_src[vi] == (void const*)g_stream_file_buf) {
			ste_voice_release_file_heap(vi);
			ste_stream_shutdown_one(&g_voice_ss[vi]);
			ste_voice_reset_sources(vi);
			g_voice_src[vi] = 0;
		}
	}
	if (!ste_any_voice_active()) {
		digi_output_stop();
	}
	ste_sr_restore(sr);
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;

	void const* retrieved = MFCD::Retrieve(filename);
	if (retrieved) {
		return ste_play_sample(retrieved, PRIORITY_MAX, volume, 1);
	}

	/* Uncached AUDX meta only — classic AUD is not played via File_Stream. */
	CCFileClass file(filename);
	if (!file.Is_Available()) {
		return -1;
	}
	long const sz = file.Size();
	if (sz != (long)AUDX_PREFIX_SIZE) {
		return -1;
	}
	unsigned char* buf = (unsigned char*)malloc((unsigned long)AUDX_PREFIX_SIZE);
	if (!buf) {
		return -1;
	}
	if (!file.Open(READ) || file.Read(buf, (long)AUDX_PREFIX_SIZE) != (long)AUDX_PREFIX_SIZE) {
		free(buf);
		return -1;
	}
	file.Close();
	if (!AUDX_Is_Meta(buf)) {
		free(buf);
		return -1;
	}
	g_stream_file_buf = buf;
	g_stream_file_len = AUDX_PREFIX_SIZE;
	if (ste_play_sample(buf, PRIORITY_MAX, volume, 1) < 0) {
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
	StAudioDriver want = g_st_audio_driver_preference;

	Audio_Dma_Save_Tos_Sound();
	ste_process_pending_voice_shutdown();
	(void)stereo;
	(void)rate;
	(void)bits_per_sample; /* Digi path is always 8-bit; ignore host request. */
	ste_audio_vbl_remove();
	if (Digi_Shutdown) {
		Digi_Shutdown();
	}
	g_audio_ok = 0;
	g_st_audio_backend = ST_AUDIO_NONE;
	g_st_audio_subsample_2 = 0;
	Digi_Movie_Set_Owns(0);
	g_pending_voice_shutdown = 0;
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;
	Audio_Focus_Loss_Function = 0;

	if (want == ST_AUDIO_NONE) {
		SampleType = SAMPLE_NONE;
		SoundType = SFX_NONE;
		DBG_INFO("Audio: None (digi disabled by CONQUER.INI)");
		return FALSE;
	}

	int const dma_avail = ST_Hw_Dma_Audio_Available() ? 1 : 0;

	if (want == ST_AUDIO_AUTO) {
		if (dma_avail) {
			want = ST_AUDIO_STE;
		} else {
			DBG_WARN("Atari DMA sound not available; using YM-2149 fallback");
			want = ST_AUDIO_YM;
		}
	}

	if (want == ST_AUDIO_STE) {
		if (!dma_avail) {
			printf("STE-DMA: Audio_Init failed (STE requested, no DMA)\n");
			fflush(stdout);
			SampleType = SAMPLE_NONE;
			SoundType = SFX_NONE;
			return FALSE;
		}
		g_st_audio_backend = ST_AUDIO_STE;
		if (!Audio_Dma_Init()) {
			if (g_st_audio_driver_preference == ST_AUDIO_AUTO) {
				DBG_WARN("Atari DMA sound not available; using YM-2149 fallback");
				g_st_audio_backend = ST_AUDIO_NONE;
				want = ST_AUDIO_YM;
			} else {
				printf("STE-DMA: Audio_Init failed (DMA ring ST-RAM)\n");
				fflush(stdout);
				g_st_audio_backend = ST_AUDIO_NONE;
				SampleType = SAMPLE_NONE;
				SoundType = SFX_NONE;
				return FALSE;
			}
		} else {
			g_ste_pcm_dup2x = (Digi_Info && Digi_Info()->device_rate_hz >= 16000u) ? 1 : 0;
			for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
				g_voice_pcm[vi].reset();
				g_voice_ima[vi].reset();
			}
			SampleType = SAMPLE_SB;
			SoundType = SFX_DMA_SOUND;
			g_audio_ok = 1;
			ste_audio_vbl_install();
			DBG_INFO("STE-DMA: Audio_Init OK (%u Hz mono, dup2x=%d, hw=%d)",
			    Digi_Info ? Digi_Info()->device_rate_hz : 0u,
			    g_ste_pcm_dup2x,
			    ST_Hw_Machine_Major());
			return TRUE;
		}
	}

	if (want == ST_AUDIO_YM || want == ST_AUDIO_COVOX) {
		g_ste_pcm_dup2x = 0;
		g_st_audio_backend = want;
		if (!Timer_Dac_Init(want)) {
			g_st_audio_backend = ST_AUDIO_NONE;
			SampleType = SAMPLE_NONE;
			SoundType = SFX_NONE;
			return FALSE;
		}
		g_st_audio_subsample_2 =
		    (Digi_Info && Digi_Info()->device_rate_hz < 10000u) ? 1 : 0;
		for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
			g_voice_pcm[vi].reset();
			g_voice_ima[vi].reset();
		}
		if (!Digi_Submit) {
			if (Digi_Shutdown) {
				Digi_Shutdown();
			}
			g_st_audio_backend = ST_AUDIO_NONE;
			g_st_audio_subsample_2 = 0;
			SampleType = SAMPLE_NONE;
			SoundType = SFX_NONE;
			return FALSE;
		}
		SampleType = SAMPLE_SB;
		SoundType = SFX_DMA_SOUND;
		g_audio_ok = 1;
		ste_audio_vbl_install();
		DBG_INFO("Timer-DAC: Audio_Init OK (%s, ~%u Hz, subsample=%d)",
		    ST_Audio_Cfg_Driver_Name(want),
		    Digi_Info ? Digi_Info()->device_rate_hz : 6250u,
		    g_st_audio_subsample_2);
		return TRUE;
	}

	SampleType = SAMPLE_NONE;
	SoundType = SFX_NONE;
	return FALSE;
}

void Sound_End(void)
{
	ste_audio_vbl_remove();
	digi_output_stop();
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
	if (Digi_Shutdown) {
		Digi_Shutdown();
	}
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;
	g_audio_ok = 0;
	g_st_audio_backend = ST_AUDIO_NONE;
	g_st_audio_subsample_2 = 0;
	Digi_Movie_Set_Owns(0);
	Audio_Dma_Restore_Tos_Sound();
}

void Stop_Sample(int)
{
	digi_output_stop();
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
			ste_voice_reset_sources(vi);
			g_voice_src[vi] = 0;
		}
	}
	if (!ste_any_voice_active()) {
		digi_output_stop();
	}
	ste_sr_restore(sr);
}

/*
 * Start a voice. as_score marks theme/file-stream voices for g_score_volume scaling
 * (must be set before ring prefill so the first buffers use the score gain).
 */
static int ste_play_sample(void const* sample, int priority, int volume, int as_score)
{
	if (!g_audio_ok || !sample || !Digi_Submit) {
		return -1;
	}
	unsigned char const* b = (unsigned char const*)sample;
	unsigned long aud_bytes;
	if (AUDX_Is_Meta(sample)) {
		aud_bytes = (unsigned long)AUDX_PREFIX_SIZE;
	} else if (sample == (void const*)g_stream_file_buf && g_stream_file_len >= (unsigned long)STE_AUD_HDR_LEN) {
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
			ste_voice_reset_sources(vi);
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
	if (as_score) {
		ste_voice_set_score(&g_voice_ss[vi], 1);
	}
	g_voice_ss[vi].play_priority = priority;
	g_voice_src[vi] = sample;
	if (sample == (void const*)g_stream_file_buf) {
		g_stream_file_voice = vi;
	}

	/* Prefill page-pointer ring before VBL can pull (main thread; may GEMDOS). */
	g_voice_page_src[vi].service();

	if (!cold_arm) {
		/*
		 * Overlay voices should start at their own sample start; they join the mix on the
		 * next service pass rather than inheriting elapsed time from an older stream.
		 */
		return 1;
	}

	/* Half-ring prefill via Digi_Submit (arms device on first write). */
	digi_output_stop();
	g_stream_samples_written = 0UL;
	{
		unsigned const prefill =
		    Digi_Info ? (Digi_Info()->ring_samples / 2U) : ((unsigned)STE_DMA_RING_SAMPLES / 2U);
		digi_submit_mixed(prefill & ~1U);
	}
	g_ste_suppress_dma_off_cleanup = 0;
	return 1;
}

int Play_Sample(void const* sample, int priority, int volume, signed short)
{
	return ste_play_sample(sample, priority, volume, 0);
}

int Play_Sample_Handle(void const* sample, int priority, int volume, signed short panloc, int)
{
	return Play_Sample(sample, priority, volume, panloc);
}

int Set_Sound_Vol(int) { return 0; }

int Set_Score_Vol(int volume)
{
	int const old = g_score_volume;
	g_score_volume = Bound(volume, 0, 0xFF);

	/*
	 * Live update: rebuild LUTs for active score voices so the next VBL mix
	 * pulls at the new level (ring latency may leave a short tail at the old gain).
	 */
	unsigned short const sr = ste_sr_lock_ipl5();
	for (int vi = 0; vi < STE_MIX_VOICES; ++vi) {
		struct SteStreamState* ss = &g_voice_ss[vi];
		if (ss->active && ss->is_score) {
			ste_voice_rebuild_vol_lut(ss);
		}
	}
	ste_sr_restore(sr);
	return old;
}
void Fade_Sample(int handle, int)
{
	/* TODO: replace this temporary Atari behavior with a real per-handle fade. */
	Stop_Sample(handle);
}
int Get_Free_Sample_Handle(int) { return 1; }
int Get_Digi_Handle(void) { return g_audio_ok ? 1 : -1; }

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
 * Yield digi HAL to STVQ: stop voices, Flush, remove mixer VBL.
 * Digi_* hooks and backend ring stay installed for Digi_Submit from the player.
 */
void Ste_Audio_Yield_Dma(void)
{
	ste_process_pending_voice_shutdown();
	ste_shutdown_all_voices();
	g_stream_file_voice = -1;
	digi_output_stop();
	ste_audio_vbl_remove();
	Digi_Movie_Set_Owns(1);
	if (Digi_Flush) {
		Digi_Flush();
	}
	if (g_st_stvq_enable_audio) {
		if (Digi_Resume) {
			Digi_Resume();
		}
	} else if (Digi_Pause) {
		Digi_Pause();
	}
}

/*
 * Reclaim digi after STVQ: Flush, reinstall mixer VBL; idle until Theme/Play_Sample arms.
 */
void Ste_Audio_Reclaim_Dma(void)
{
	if (!g_audio_ok) {
		return;
	}
	Digi_Movie_Set_Owns(0);
	if (Digi_Flush) {
		Digi_Flush();
	}
	if (Audio_Dma_Inited()) {
		Audio_Dma_Connect_Output();
	}
	ste_audio_vbl_install();
}

BOOL Set_Primary_Buffer_Format(void) { return TRUE; }
BOOL Start_Primary_Sound_Buffer(BOOL) { return TRUE; }
void Stop_Primary_Sound_Buffer(void) { digi_output_stop(); }
