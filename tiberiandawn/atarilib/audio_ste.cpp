/*
 * audio_ste.cpp - Atari STe DMA 8-bit mono PCM output for digitized SFX (.AUD in MIX).
 *
 * Requires supervisor (startup calls Super(0)). Uses cookie _MCH for STE-class hardware
 * (STE / Mega STE / TT / Falcon). DMA sound is programmed at a **fixed 25033 Hz mono 8-bit**
 * rate ($FF8921 = 0x82). All source material is assumed to already be at that rate, so no
 * resampling is done. Mono IMA99 decodes directly into the ring as signed 8-bit with volume
 * already baked in via a per-sample LUT; raw PCM goes through build_pcm8_mono to be biased /
 * volume-scaled to signed 8-bit. Playback uses two 1024-sample DMA slots in ST-RAM (playing +
 * queued), with the hardware looping start/end addresses via end-of-sweep auto-reload.
 *
 * Sound_Callback is driven by the STE DMA frame address counter at $FF8909/B/D: each call reads
 * the counter to find the currently-playing slot, no-ops while it is still inside the same slot,
 * and on every detected slot transition refills the just-consumed slot with the next stream
 * chunk and rewrites the sweep registers for the next loop. No software timing reference is used.
 *
 * Compression 0 = raw PCM, 99 = Westwood AUD (0xDEAF-framed IMA ADPCM). Stereo is not supported.
 */

#include "function.h"
#include "ccfile.h"
#include "adpcm-68k/ws_adpcm_68k.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>

extern Sample_Type SampleType;
extern SFX_Type SoundType;

void (*Audio_Focus_Loss_Function)(void) = 0;

enum { COOKIE_JAR_PTR = 0x000005A0UL };
/* Same as MiNT C__MCH: '_','M','C','H' as big-endian longword */
enum { COOKIE_MCH = 0x5F4D4348UL };

enum { AUD_HDR_LEN = 12 };
enum { AUD99_FRAME_MAGIC = 0x0000DEAFUL };
enum { AUD_COMP_PCM = 0, AUD_COMP_IMA99 = 99 };
/* One-shot limits (large IMA scores / long voice). Tune down on very small RAM systems. */
enum { AUD99_MAX_COMPRESSED_PAYLOAD = 2UL * 1024UL * 1024UL }; /* bytes after 12-byte AUD header */
enum { AUD99_MAX_DECODED_PCM_BYTES = 8UL * 1024UL * 1024UL };  /* 16-bit PCM from AUD */
enum { STE_DMA_CHUNK_SAMPLES = 1024 }; /* 8-bit mono samples per DMA submit (~40.9 ms @ 25033 Hz) */
/*
 * Two-slot ping-pong: one slot is playing while the other is queued in $FF8903/F (the hardware
 * re-reads start/end at every end-of-sweep). Sound_Callback detects the slot transition and
 * refills the just-consumed slot with the next stream chunk, re-writing the sweep registers so
 * DMA loops back into it. Only ever one slot is "enqueued" at a time.
 */
enum { STE_DMA_NUM_BUFS = 2 };
enum { STE_PCM_RING_BYTES = 65536u };
enum { AUD99_MAX_SINGLE_FRAME_PCM = 262144u }; /* refuse single IMA frame larger than this */
/* Fixed DMA output rate. $FF8921 rate bits %10 = 25033 Hz. */
enum { STE_HW_RATE_IDX = 2 };

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

static void const* g_playing_src;
static int g_playing;

/* Loaded by File_Stream_Sample_Vol for theme scores (THEME.CPP); one buffer at a time. */
static unsigned char* g_stream_file_buf;
static unsigned long g_stream_file_len;

static void* ste_stram_alloc(unsigned long nbytes)
{
	if (nbytes == 0UL) {
		return 0;
	}
	long const a = Mxalloc((long)nbytes, MX_STRAM | MX_PRIVATE);
	return a > 0L ? (void*)a : (void*)0;
}

static void ste_stram_free(void* p)
{
	if (p) {
		Mfree(p);
	}
}

struct AdpcmChan {
	int predictor;
	int step_index;
};

struct Ima99Stream {
	unsigned char const* pay;
	unsigned long pay_len;
	unsigned char const* next_hdr;
	int channels;
	AdpcmChan ch[2];
	WsAdpcm68kState ws_mono;
	unsigned frame_fill;
	unsigned frame_pos;
	int volume;                 /* Captured at open for debug/info; LUT is the decode input. */
	signed char vol_lut[256];   /* (s8 * volume) >> 8 for s8 in [-128, 127]. */
};

struct SteStreamState {
	int active;
	unsigned char compression;
	int pcm_layout_flags;
	int volume;
	unsigned in_stride;
	unsigned long pcm_cap_bytes;
	unsigned chunks_total;
	/* Decoded source PCM queue at ring[0..ring_used) -- 16-bit LE for IMA99, 8-bit for raw. */
	unsigned char* ring;
	unsigned ring_cap;
	unsigned ring_used;
	struct Ima99Stream ima;
	unsigned char const* raw_ptr;
	unsigned long raw_left;
};

static struct SteStreamState g_ss;
static unsigned char* g_dma_pool;
static unsigned char* g_ima_frame_scratch;
static unsigned g_ima_frame_scratch_cap;
/*
 * Streaming state, updated only from Sound_Callback after the initial prefill in Play_Sample.
 *
 *   g_fill_next_chunk: stream-level chunk id that will be written into the just-consumed slot
 *                      on the next detected transition. Past the end of the stream,
 *                      ste_stream_fill_dma_slot produces silence instead.
 *   g_last_cur_slot:   the slot index (0 or 1) DMA was observed in on the previous callback.
 *                      A change signals a single transition; same value means no transition.
 *   g_chunks_played:   running total of real (pre-EOS) chunks consumed by DMA; once this
 *                      reaches chunks_total, Sound_Callback stops the hardware.
 */
static unsigned g_fill_next_chunk;
static int g_last_cur_slot;
static unsigned g_chunks_played;

static unsigned long ste_dma_read_counter(void);

static unsigned short read_le16(unsigned char const* p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static unsigned long read_le32(unsigned char const* p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16)
	       | ((unsigned long)p[3] << 24);
}

/* build_pcm8_mono uses read_le16 (LE) on sample bytes; native short stores are BE on m68k. */
#if defined(BIG_ENDIAN)
static void pcm16le_swap_block(unsigned char* buf, unsigned long nbytes)
{
	for (unsigned long i = 0; i + 1 < nbytes; i += 2) {
		unsigned char t = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = t;
	}
}
#endif

/* IMA ADPCM step/index tables (same as FFmpeg libavcodec/adpcm_data.c, ADPCM reference). */
static short const g_ima_step_table[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
	1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
	8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};
static signed char const g_ima_index_table[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

#if defined(__GNUC__)
#define IMA99_ALWAYS_INLINE __attribute__((always_inline)) static inline
#else
#define IMA99_ALWAYS_INLINE static inline
#endif

/* Westwood IMA (FFmpeg adpcm_ima_expand_nibble with shift=3). Inlined for mono/stereo inner loops. */
IMA99_ALWAYS_INLINE short ima_ws_expand_nibble(AdpcmChan* c, int nibble)
{
	int const step = g_ima_step_table[c->step_index];
	int si = c->step_index + (int)g_ima_index_table[(unsigned)nibble & 15U];
	if (si < 0) {
		si = 0;
	} else if (si > 88) {
		si = 88;
	}
	int const diff = ((2 * (nibble & 7) + 1) * step) >> 3;
	int pred = c->predictor;
	pred = (nibble & 8) ? (pred - diff) : (pred + diff);
	if (pred > 32767) {
		pred = 32767;
	} else if (pred < -32768) {
		pred = -32768;
	}
	c->predictor = pred;
	c->step_index = si;
	return (short)pred;
}

/*
 * Decode AUD compression 99: concatenated frames, each
 *   u16 comp_size, u16 decomp_size, u32 magic 0x0000DEAF, comp_size bytes of nibbles.
 * ADPCM state carries across frames (Westwood VQA/AUD convention).
 *
 * Mono: per-byte low then high nibble; when the chunk's OutputSize is reached, remaining
 * nibbles in that chunk are skipped without updating ADPCM state (matches OpenRA
 * ImaAdpcmAudStream / on-disk Westwood padding on long clips).
 *
 * On success sets *actual_pcm_bytes to decoded PCM length (even, > 0).
 * buf_cap must be >= the sum of all chunk decomp sizes (may exceed AUD UncompSize).
 *
 * Mono: some WW assets (e.g. CLOCK1.AUD) declare DecompressedSize slightly larger than
 * nibbles in CompressedSize allow (max PCM bytes = comp * 4). Clamp per frame accordingly.
 */
static void ima99_stream_init(struct Ima99Stream* s, unsigned char const* payload, unsigned long payload_len, int channels)
{
	memset(s, 0, sizeof(*s));
	s->pay = payload;
	s->pay_len = payload_len;
	s->next_hdr = payload;
	s->channels = channels;
	ws_adpcm68k_init_tables();
}

/* Decode next full IMA99 frame into g_ima_frame_scratch; ADPCM state carries in s->ch[]. Returns 0 if EOF/error. */
static int ima99_stream_decode_next_frame(struct Ima99Stream* s)
{
	unsigned char const* const pay_end = s->pay + s->pay_len;
	if (s->next_hdr + 8 > pay_end) {
		return 0;
	}
	unsigned comp = read_le16(s->next_hdr);
	unsigned decomp = read_le16(s->next_hdr + 2);
	unsigned magic = (unsigned)read_le32(s->next_hdr + 4);
	if (magic != AUD99_FRAME_MAGIC || comp == 0 || decomp == 0 || (decomp & 1U) != 0 || s->next_hdr + 8 + comp > pay_end) {
		return 0;
	}
	unsigned char const* chunk = s->next_hdr + 8;
	s->next_hdr += 8 + comp;
	unsigned frame_pcm = decomp;
	if (s->channels == 1) {
		unsigned const cap = comp * 4U;
		if (frame_pcm > cap) {
			frame_pcm = cap;
		}
	}
	if (frame_pcm == 0 || (frame_pcm & 1U) != 0 || frame_pcm > g_ima_frame_scratch_cap || frame_pcm > AUD99_MAX_SINGLE_FRAME_PCM) {
		return 0;
	}
	unsigned char* dst = g_ima_frame_scratch;
	unsigned char const* cptr = chunk;

	if (s->channels == 1) {
		/*
		 * Mono fast path: decode straight to signed 8-bit DMA samples with volume already
		 * applied. Eliminates the 16-bit intermediate buffer, the LE byte-swap pass on m68k,
		 * and the separate build_pcm8_mono pass later in ste_stream_fill_dma_slot. Stored
		 * frame_fill is now in 8-bit sample-bytes (= frame_samples), not 16-bit byte pairs.
		 */
		unsigned const frame_samples = frame_pcm >> 1;
		ws_adpcm68k_decode_mono8(&s->ws_mono, chunk, comp, (signed char*)dst, frame_samples, s->vol_lut);
		s->ch[0].predictor = s->ws_mono.predictor;
		s->ch[0].step_index = s->ws_mono.step_index;
		s->frame_fill = frame_samples;
		s->frame_pos = 0;
		return 1;
	} else {
		short* samples = (short*)dst;
		int buf_size = (int)comp;
		int nb_samples = buf_size * 2 / s->channels;
		if (nb_samples <= 0) {
			return 0;
		}
		int const st = 1;
		short* sp = samples;
		AdpcmChan ch[2];
		ch[0] = s->ch[0];
		ch[1] = s->ch[1];
		for (int n = nb_samples / 2; n > 0; n--) {
			for (int channel = 0; channel < s->channels; channel++) {
				int v = *cptr++;
				*sp++ = ima_ws_expand_nibble(&ch[channel], (int)(v & 15));
				sp[st] = ima_ws_expand_nibble(&ch[channel], (int)(v >> 4));
			}
			sp += s->channels;
		}
		s->ch[0] = ch[0];
		s->ch[1] = ch[1];
		if ((unsigned)(cptr - chunk) != comp) {
			return 0;
		}
		if ((unsigned)((sp - samples) * 2) != decomp) {
			return 0;
		}
	}
#if defined(BIG_ENDIAN)
	pcm16le_swap_block(dst, frame_pcm);
#endif
	s->frame_fill = frame_pcm;
	s->frame_pos = 0;
	return 1;
}

/* Copy up to max_out bytes from current frame buffer into dst; may decode new frames. */
static unsigned ima99_stream_pull(struct Ima99Stream* s, unsigned char* dst, unsigned max_out)
{
	unsigned written = 0;
	while (written < max_out) {
		if (s->frame_pos >= s->frame_fill) {
			if (!ima99_stream_decode_next_frame(s)) {
				break;
			}
		}
		unsigned const n = s->frame_fill - s->frame_pos;
		unsigned const take = max_out - written < n ? max_out - written : n;
		memcpy(dst + written, g_ima_frame_scratch + s->frame_pos, take);
		s->frame_pos += take;
		written += take;
	}
	return written;
}

static int ste_class_machine(void)
{
	unsigned long* jar = *(unsigned long**)COOKIE_JAR_PTR;
	if (!jar) {
		return 0;
	}
	for (; jar[0] != 0; jar += 2) {
		if (jar[0] == COOKIE_MCH) {
			unsigned long v = jar[1];
			return (v >> 16) != 0;
		}
	}
	return 0;
}

static void ste_dma_mixer_connect(void)
{
	if (!g_ste_dma_ok) {
		return;
	}
	*STE_DMA_MIXER = 0x03;
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
	*STE_DMA_CTRL = 0;
	/* $FF8901 low bits: DMA off (many STE docs: %01/%11 = on; %00 = off). */
	*STE_DMA_MODE = 0;
}

/*
 * Convert `n` source samples into signed 8-bit DMA samples with a scalar volume attenuation.
 *
 * Source is either 16-bit LE signed (IMA99 output) or 8-bit unsigned (raw PCM). Both are assumed
 * to already be at the DMA hardware rate (25033 Hz mono), so this is a straight per-sample
 * conversion -- no resampling, no channel mixing. The volume step is `(s * vol) >> 8`; at full
 * volume (vol=255) that attenuates by ~0.4% which is inaudible and avoids a per-sample divide.
 */
static void build_pcm8_mono(unsigned char const* src, int flags, unsigned n, int volume, unsigned char* dst)
{
	int const sixteen = (flags & AUD_FLAG_16BIT) != 0;
	int const vol = Bound(volume, 0, 0xFF);
	if (sixteen) {
		/* Little-endian 16-bit signed -> high byte is already a signed 8-bit sample. */
		for (unsigned i = 0; i < n; ++i) {
			int const s = (signed char)src[i * 2 + 1];
			dst[i] = (unsigned char)((s * vol) >> 8);
		}
	} else {
		/* Unsigned 8-bit (silence=128) -> signed 8-bit (silence=0). */
		for (unsigned i = 0; i < n; ++i) {
			int const s = (int)src[i] - 128;
			dst[i] = (unsigned char)((s * vol) >> 8);
		}
	}
}

static unsigned char* ste_dma_slot_ptr(int slot)
{
	if (!g_dma_pool || slot < 0 || slot >= STE_DMA_NUM_BUFS) {
		return 0;
	}
	return g_dma_pool + (unsigned)slot * (unsigned)STE_DMA_CHUNK_SAMPLES;
}

static int ste_stream_alloc_pools(void)
{
	if (g_dma_pool) {
		return 1;
	}
	unsigned long const dma_bytes = (unsigned long)STE_DMA_CHUNK_SAMPLES * (unsigned long)STE_DMA_NUM_BUFS;
	g_dma_pool = (unsigned char*)ste_stram_alloc(dma_bytes);
	if (!g_dma_pool) {
		return 0;
	}
	g_ss.ring = (unsigned char*)ste_stram_alloc(STE_PCM_RING_BYTES);
	if (!g_ss.ring) {
		ste_stram_free(g_dma_pool);
		g_dma_pool = 0;
		return 0;
	}
	g_ss.ring_cap = STE_PCM_RING_BYTES;
	g_ima_frame_scratch_cap = 65536u;
	if (g_ima_frame_scratch_cap > AUD99_MAX_SINGLE_FRAME_PCM) {
		g_ima_frame_scratch_cap = AUD99_MAX_SINGLE_FRAME_PCM;
	}
	g_ima_frame_scratch = (unsigned char*)ste_stram_alloc(g_ima_frame_scratch_cap);
	if (!g_ima_frame_scratch) {
		ste_stram_free(g_ss.ring);
		ste_stram_free(g_dma_pool);
		g_ss.ring = g_dma_pool = 0;
		return 0;
	}
	return 1;
}

static void ste_stream_free_pools(void)
{
	ste_stram_free(g_ima_frame_scratch);
	g_ima_frame_scratch = 0;
	g_ima_frame_scratch_cap = 0;
	ste_stram_free(g_ss.ring);
	g_ss.ring = 0;
	g_ss.ring_cap = 0;
	g_ss.ring_used = 0;
	ste_stram_free(g_dma_pool);
	g_dma_pool = 0;
}

static void ste_stream_ring_consume(struct SteStreamState* ss, unsigned n)
{
	if (n == 0 || n > ss->ring_used) {
		return;
	}
	memmove(ss->ring, ss->ring + n, ss->ring_used - n);
	ss->ring_used -= n;
}

/*
 * Top up the PCM ring by at most `want_bytes - ring_used` bytes. This is called with
 * want_bytes == one DMA chunk's worth (1024 bytes of 16-bit mono at most), so the decode work
 * is spread evenly across TX callbacks: every callback pays ~one chunk's decode instead of
 * every 8th callback paying for 8 chunks' worth in one ~24 ms burst. The IMA decoder keeps any
 * leftover samples from a partially consumed frame in its own state, so passing a small cap
 * does not cost extra frame parsing.
 */
static void ste_stream_feed_ring(struct SteStreamState* ss, unsigned want_bytes)
{
	while (ss->ring_used < want_bytes) {
		unsigned const room = ss->ring_cap - ss->ring_used;
		if (room < 2) {
			break;
		}
		unsigned const need = want_bytes - ss->ring_used;
		unsigned const chunk = room > need ? need : room;
		if (ss->compression == AUD_COMP_IMA99) {
			unsigned const got = ima99_stream_pull(&ss->ima, ss->ring + ss->ring_used, chunk);
			if (got == 0) {
				break;
			}
			ss->ring_used += got;
		} else {
			unsigned const take = ss->raw_left < (unsigned long)chunk ? (unsigned)ss->raw_left : chunk;
			if (take == 0) {
				break;
			}
			memcpy(ss->ring + ss->ring_used, ss->raw_ptr, take);
			ss->raw_ptr += take;
			ss->raw_left -= take;
			ss->ring_used += take;
		}
	}
}

/*
 * Fill one 512-byte DMA slot from the stream state. Source is already at the DMA rate, so we just
 * pull (chunk_samples * in_stride) bytes from the ring, convert them in place to signed 8-bit,
 * and pad any short tail with DMA silence (0).
 */
static int ste_stream_fill_dma_slot(int slot_idx, unsigned chunk_id)
{
	struct SteStreamState* ss = &g_ss;
	unsigned char* const dst = ste_dma_slot_ptr(slot_idx);
	if (!dst) {
		return 0;
	}
	if (chunk_id >= ss->chunks_total) {
		memset(dst, 0, (size_t)STE_DMA_CHUNK_SAMPLES);
		return 1;
	}
	unsigned const need_bytes = (unsigned)STE_DMA_CHUNK_SAMPLES * ss->in_stride;
	ste_stream_feed_ring(ss, need_bytes);

	unsigned const have_bytes = ss->ring_used < need_bytes ? ss->ring_used : need_bytes;
	unsigned const have_samples = have_bytes / ss->in_stride;
	/*
	 * IMA99 mono is decoded directly into the ring as signed 8-bit DMA samples with volume
	 * already baked in (ws_adpcm68k_decode_mono8), so the "build" phase collapses to a plain
	 * memcpy. Raw PCM still needs the unsigned->signed (and optional 16->8-bit) conversion via
	 * build_pcm8_mono.
	 */
	if (ss->compression == AUD_COMP_IMA99 && ss->in_stride == 1U) {
		memcpy(dst, ss->ring, have_samples);
	} else {
		build_pcm8_mono(ss->ring, ss->pcm_layout_flags, have_samples, ss->volume, dst);
	}

	if (have_samples < (unsigned)STE_DMA_CHUNK_SAMPLES) {
		memset(dst + have_samples, 0, (size_t)STE_DMA_CHUNK_SAMPLES - have_samples);
	}
	ste_stream_ring_consume(ss, have_samples * ss->in_stride);
	return 1;
}

static void ste_stream_shutdown(void)
{
	memset(&g_ss.ima, 0, sizeof(g_ss.ima));
	g_ss.active = 0;
	g_ss.compression = 0;
	g_ss.pcm_layout_flags = 0;
	g_ss.volume = 0;
	g_ss.in_stride = 0;
	g_ss.pcm_cap_bytes = 0;
	g_ss.chunks_total = 0;
	g_ss.ring_used = 0;
	g_ss.raw_ptr = 0;
	g_ss.raw_left = 0;
}

/*
 * Parse the 12-byte AUD header and prime the streaming state. The AUD's nominal sample rate is
 * ignored -- callers are expected to have pre-converted the asset to 25033 Hz mono, the fixed
 * DMA output rate. Stereo payloads are rejected.
 */
static int ste_stream_open(unsigned char const* b, unsigned long aud_bytes, int volume)
{
	unsigned char* const ring_keep = g_ss.ring;
	unsigned const ring_cap_keep = g_ss.ring_cap;
	memset(&g_ss, 0, sizeof(g_ss));
	g_ss.ring = ring_keep;
	g_ss.ring_cap = ring_cap_keep;
	if (aud_bytes < AUD_HDR_LEN) {
		return 0;
	}
	unsigned long const size_file = read_le32(b + 2);
	unsigned long const uncomp = read_le32(b + 6);
	unsigned char const flags = b[10];
	unsigned char const compression = b[11];
	if ((flags & AUD_FLAG_STEREO) != 0) {
		return 0;
	}
	/*
	 * `aud_stride` describes the pre-decode AUD sample width advertised in the AUD header
	 * (used to turn `uncomp` -- which is raw PCM bytes produced by the decoder -- into a
	 * sample count). `in_stride` / `pcm_layout_flags` describe the layout that ends up in
	 * the PCM ring and drives ste_stream_fill_dma_slot. These two used to be the same, but
	 * mono IMA99 now decodes straight to signed 8-bit in the ring (ws_adpcm68k_decode_mono8),
	 * so its ring layout is stride=1, no 16-bit flag, regardless of what the AUD header says.
	 * Stereo IMA99 (still 16-bit intermediate) would keep the header layout, but it is
	 * rejected by the AUD_FLAG_STEREO check above so in practice mono collapses to stride 1.
	 */
	unsigned const aud_stride = (flags & AUD_FLAG_16BIT) ? 2U : 1U;
	int pcm_layout_flags = (int)flags;
	unsigned in_stride = aud_stride;
	if (compression == AUD_COMP_IMA99) {
		if (size_file == 0 || size_file > AUD99_MAX_COMPRESSED_PAYLOAD || uncomp == 0 || (uncomp & 1U) != 0
		    || uncomp > AUD99_MAX_DECODED_PCM_BYTES) {
			return 0;
		}
		/* Mono IMA99 ring is 8-bit signed; the AUD header's 16-bit flag applies only to the
		 * pre-decode intermediate, which we bypass. */
		pcm_layout_flags &= ~(int)AUD_FLAG_16BIT;
		in_stride = 1U;
	} else if (compression != AUD_COMP_PCM) {
		return 0;
	}
	unsigned long pcm_cap = uncomp;
	if (compression == AUD_COMP_PCM && pcm_cap == 0 && aud_bytes > (unsigned long)AUD_HDR_LEN) {
		pcm_cap = aud_bytes - (unsigned long)AUD_HDR_LEN;
	}
	if (pcm_cap == 0 || pcm_cap > AUD99_MAX_DECODED_PCM_BYTES) {
		return 0;
	}
	unsigned long const payload_avail = aud_bytes > AUD_HDR_LEN ? aud_bytes - AUD_HDR_LEN : 0;
	unsigned long payload_len = size_file;
	if (payload_len > payload_avail) {
		payload_len = payload_avail;
	}
	if (compression == AUD_COMP_IMA99 && payload_len == 0) {
		return 0;
	}
	unsigned long const src_samples = pcm_cap / aud_stride;
	unsigned const chunks = (unsigned)((src_samples + (unsigned long)STE_DMA_CHUNK_SAMPLES - 1UL) / (unsigned long)STE_DMA_CHUNK_SAMPLES);
	if (chunks == 0) {
		return 0;
	}
	g_ss.active = 1;
	g_ss.compression = compression;
	g_ss.pcm_layout_flags = pcm_layout_flags;
	g_ss.volume = volume;
	g_ss.in_stride = in_stride;
	g_ss.pcm_cap_bytes = pcm_cap;
	g_ss.chunks_total = chunks;
	g_ss.ring_used = 0;
	if (compression == AUD_COMP_IMA99) {
		ima99_stream_init(&g_ss.ima, b + AUD_HDR_LEN, payload_len, 1);
		g_ss.ima.volume = volume;
		ws_adpcm68k_build_volume_lut(g_ss.ima.vol_lut, volume);
	} else {
		g_ss.raw_ptr = b + AUD_HDR_LEN;
		g_ss.raw_left = payload_avail < pcm_cap ? payload_avail : pcm_cap;
	}
	return 1;
}

/*
 * Program the next sweep's start/end addresses without touching the DMA control bits.
 * In loop mode the hardware reloads start/end at end-of-sweep, so as long as we rewrite
 * these mid-sweep (well before the boundary) the next iteration plays the new buffer
 * seamlessly -- no stop/restart, no silence gap.
 */
static void ste_dma_write_sweep(unsigned char const* start, unsigned len)
{
	unsigned long const s = (unsigned long)start;
	unsigned long const e = s + (unsigned long)len;
	ste_dma_set_address(STE_DMA_START_H, s);
	ste_dma_set_address(STE_DMA_END_H, e);
}

/* Initial arm: stop DMA, set rate/mono mode, load first sweep, then enable play + loop. */
static void ste_dma_arm_loop(unsigned char const* first, unsigned len)
{
	ste_dma_stop();
	ste_dma_mixer_connect();
	*STE_DMA_SOUND_MODE = (unsigned char)(STE_DMA_SND_MODE_MONO | STE_HW_RATE_IDX);
	ste_dma_write_sweep(first, len);
	/* $FF8901 bits 0+1: %11 = play with loop (auto-reload start/end at end-of-sweep). */
	*STE_DMA_MODE = 0x03u;
}

/*
 * Read the 24-bit STE DMA frame address counter ($FF8909/B/D). The three bytes update
 * asynchronously as DMA fetches; for our use (locating the current 512-byte slot) the
 * occasional tear is harmless because the slot boundaries are 512 bytes apart.
 */
static unsigned long ste_dma_read_counter(void)
{
	unsigned long const h = (unsigned long)*STE_DMA_CNT_H;
	unsigned long const m = (unsigned long)*STE_DMA_CNT_M;
	unsigned long const l = (unsigned long)*STE_DMA_CNT_L;
	return (h << 16) | (m << 8) | l;
}

static void ste_stream_stop_and_cleanup(void)
{
	void const* const was = g_playing_src;
	ste_dma_stop();
	g_playing = 0;
	g_playing_src = 0;
	ste_stream_shutdown();
	if (was && was == (void const*)g_stream_file_buf) {
		free(g_stream_file_buf);
		g_stream_file_buf = 0;
		g_stream_file_len = 0;
	}
}

/*
 * Ping-pong callback: at most one stream chunk is produced per invocation. On each detected
 * slot transition we refill the slot DMA just consumed with the next stream chunk, then reprogram
 * the sweep registers so the hardware loops back into that freshly-filled slot at its next
 * end-of-sweep. Extra calls within the same slot are cheap no-ops.
 */
void Sound_Callback(void)
{
	if (!g_ste_dma_ok || !g_playing || !g_ss.active) {
		return;
	}
	if ((*STE_DMA_MODE & 0x01u) == 0u) {
		ste_stream_stop_and_cleanup();
		return;
	}

	unsigned long const cnt = ste_dma_read_counter();
	int cur_slot = -1;
	for (int i = 0; i < STE_DMA_NUM_BUFS; ++i) {
		unsigned long const base = (unsigned long)ste_dma_slot_ptr(i);
		if (cnt >= base && cnt < base + (unsigned long)STE_DMA_CHUNK_SAMPLES) {
			cur_slot = i;
			break;
		}
	}
	/* Counter in transit between bytes, or slot already serviced: nothing to do. */
	if (cur_slot < 0 || cur_slot == g_last_cur_slot) {
		return;
	}
	g_last_cur_slot = cur_slot;
	++g_chunks_played;

	if (g_ss.chunks_total != 0u && g_chunks_played >= g_ss.chunks_total) {
		ste_stream_stop_and_cleanup();
		return;
	}

	int const other = cur_slot ^ 1;
	unsigned const chunk_id = g_fill_next_chunk;
	ste_stream_fill_dma_slot(other, chunk_id);
	++g_fill_next_chunk;
	ste_dma_write_sweep(ste_dma_slot_ptr(other), STE_DMA_CHUNK_SAMPLES);
}

int File_Stream_Sample(char const* filename, BOOL real_time_start)
{
	return File_Stream_Sample_Vol(filename, 0xFF, real_time_start);
}

/*
 * Theme scores (THEME.CPP) call this for "*.AUD" on disk. Win32 streamed from file;
 * here we load the whole file then play through the same STE path as SFX.
 */
int File_Stream_Sample_Vol(char const* filename, int volume, BOOL)
{
	if (!g_ste_dma_ok || !filename) {
		return -1;
	}
	if (g_playing && g_playing_src == (void const*)g_stream_file_buf) {
		ste_dma_stop();
		g_playing = 0;
		g_playing_src = 0;
		ste_stream_shutdown();
	}
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;

	CCFileClass file(filename);
	if (!file.Is_Available()) {
		return -1;
	}
	long const sz = file.Size();
	if (sz < 12 || sz > (long)(AUD99_MAX_COMPRESSED_PAYLOAD + (long)AUD_HDR_LEN)) {
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
	if (Play_Sample(buf, 0, volume, 0) < 0) {
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

BOOL Audio_Init(HWND, int bits_per_sample, BOOL stereo, int rate, int)
{
	(void)stereo;
	(void)rate;
	/* Request 8 from startup; 16 is harmless (sources may still be 16-bit in .AUD flags). */
	(void)bits_per_sample;
	g_ste_dma_ok = ste_class_machine() ? 1 : 0;
	g_playing = 0;
	g_playing_src = 0;
	ste_stream_shutdown();
	ste_stream_free_pools();
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;
	Audio_Focus_Loss_Function = 0;
	if (!g_ste_dma_ok) {
		printf("STE-DMA: Audio_Init failed (no STE-class _MCH or cookie jar)\n");
		fflush(stdout);
		SampleType = SAMPLE_NONE;
		SoundType = SFX_NONE;
		return FALSE;
	}
	SampleType = SAMPLE_SB;
	SoundType = SFX_NONE;
	ste_dma_stop();
	ste_dma_mixer_connect();
	return TRUE;
}

void Sound_End(void)
{
	ste_dma_stop();
	g_playing = 0;
	g_playing_src = 0;
	ste_stream_shutdown();
	ste_stream_free_pools();
	free(g_stream_file_buf);
	g_stream_file_buf = 0;
	g_stream_file_len = 0;
}

void Stop_Sample(int)
{
	void const* const was = g_playing_src;
	ste_dma_stop();
	g_playing = 0;
	g_playing_src = 0;
	ste_stream_shutdown();
	if (was && was == (void const*)g_stream_file_buf) {
		free(g_stream_file_buf);
		g_stream_file_buf = 0;
		g_stream_file_len = 0;
	}
}

BOOL Sample_Status(int) { return g_playing ? TRUE : FALSE; }

BOOL Is_Sample_Playing(void const* sample)
{
	if (!g_playing) {
		return FALSE;
	}
	return sample == g_playing_src;
}

void Stop_Sample_Playing(void const* sample)
{
	if (sample && sample == g_playing_src) {
		void const* const was = g_playing_src;
		ste_dma_stop();
		g_playing = 0;
		g_playing_src = 0;
		ste_stream_shutdown();
		if (was == (void const*)g_stream_file_buf) {
			free(g_stream_file_buf);
			g_stream_file_buf = 0;
			g_stream_file_len = 0;
		}
	}
}

int Play_Sample(void const* sample, int priority, int volume, signed short)
{
	(void)priority;
	if (!g_ste_dma_ok || !sample) {
		return -1;
	}
	unsigned char const* b = (unsigned char const*)sample;
	unsigned long aud_bytes;
	if (sample == (void const*)g_stream_file_buf && g_stream_file_len >= (unsigned long)AUD_HDR_LEN) {
		aud_bytes = g_stream_file_len;
	} else {
		unsigned long const szf = read_le32(b + 2);
		unsigned long const uncomp = read_le32(b + 6);
		unsigned char const compression = b[11];
		if (szf > AUD99_MAX_COMPRESSED_PAYLOAD) {
			return -1;
		}
		aud_bytes = (unsigned long)AUD_HDR_LEN + szf;
		if (compression == AUD_COMP_PCM && szf == 0 && uncomp > 0 && uncomp <= AUD99_MAX_DECODED_PCM_BYTES) {
			aud_bytes = (unsigned long)AUD_HDR_LEN + uncomp;
		}
	}
	if (!ste_stream_alloc_pools()) {
		return -1;
	}
	ste_dma_stop();
	ste_stream_shutdown();
	if (!ste_stream_open(b, aud_bytes, volume)) {
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
	/* Prefill both slots with chunks 0 and 1 (silence if the stream is shorter than the ring). */
	for (int si = 0; si < STE_DMA_NUM_BUFS; ++si) {
		if (!ste_stream_fill_dma_slot(si, (unsigned)si)) {
			return -1;
		}
	}
	g_fill_next_chunk = (unsigned)STE_DMA_NUM_BUFS;
	g_last_cur_slot = 0;
	g_chunks_played = 0u;
	g_playing_src = sample;
	/*
	 * Arm DMA in loop mode on slot 0, then queue slot 1 via write_sweep. The hardware re-reads
	 * $FF8903/F at each end-of-sweep, so the first wrap plays slot 0 then latches slot 1; from
	 * there Sound_Callback alternates the queued slot on every transition.
	 */
	ste_dma_arm_loop(ste_dma_slot_ptr(0), STE_DMA_CHUNK_SAMPLES);
	ste_dma_write_sweep(ste_dma_slot_ptr(1), STE_DMA_CHUNK_SAMPLES);
	g_playing = 1;
	return 1;
}

int Play_Sample_Handle(void const* sample, int priority, int volume, signed short panloc, int)
{
	return Play_Sample(sample, priority, volume, panloc);
}

int Set_Sound_Vol(int) { return 0; }
int Set_Score_Vol(int) { return 0; }
void Fade_Sample(int, int) {}
int Get_Free_Sample_Handle(int) { return 1; }
int Get_Digi_Handle(void) { return 1; }

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

BOOL Set_Primary_Buffer_Format(void) { return TRUE; }
BOOL Start_Primary_Sound_Buffer(BOOL) { return TRUE; }
void Stop_Primary_Sound_Buffer(void) { ste_dma_stop(); }
