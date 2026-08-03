/*
 * stvq_hw.c - Ping-pong LoRes, Setscreen/Setpalette+Vsync flip, STE DMA ring audio.
 *
 * Audio matches audio_ste.cpp: fixed ST-RAM ring, arm once with loop, mixer poke
 * at $FFFF8922, write head that never overtakes the DMA frame counter.
 *
 * Caller must be in supervisor mode (game startup / stvqview Super(0)).
 * Hardware and TOS sysvars are accessed directly — no Supexec.
 */
#include "stvq_hw.h"

#include "../st_hw_probe.h"
#include "stvq_prof.h"

#include <mint/cookie.h>
#include <mint/osbind.h>

#include <string.h>

enum {
	STVQ_MX_STRAM = 0,
	STVQ_STE_RATE_12517 = 1,
	STVQ_STE_RATE_25033 = 2,
	STVQ_STE_SND_MONO = 0x80
};

static volatile unsigned char *const STE_DMA_CTRL = (volatile unsigned char *)0xFF8900UL;
static volatile unsigned char *const STE_DMA_MODE = (volatile unsigned char *)0xFF8901UL;
static volatile unsigned char *const STE_DMA_SOUND_MODE = (volatile unsigned char *)0xFF8921UL;
static volatile unsigned char *const STE_DMA_START_H = (volatile unsigned char *)0xFF8903UL;
static volatile unsigned char *const STE_DMA_END_H = (volatile unsigned char *)0xFF890FUL;
static volatile unsigned char const *const STE_DMA_CNT_H = (volatile unsigned char const *)0xFF8909UL;
static volatile unsigned char const *const STE_DMA_CNT_M = (volatile unsigned char const *)0xFF890BUL;
static volatile unsigned char const *const STE_DMA_CNT_L = (volatile unsigned char const *)0xFF890DUL;
/* Same poke as audio_ste (not Microwire-framed LMC1992). */
static volatile unsigned char *const STE_DMA_MIXER = (volatile unsigned char *)0xFFFF8922UL;

static volatile unsigned char *const VID_BASE_H = (volatile unsigned char *)0xFF8201UL;
static volatile unsigned char *const VID_BASE_M = (volatile unsigned char *)0xFF8203UL;
static volatile unsigned char *const VID_BASE_L = (volatile unsigned char *)0xFF820DUL;
static volatile uint16_t *const VID_PAL = (volatile uint16_t *)0xFF8240UL;

static int dma_audio_available(void)
{
	long mch = 0;
	long snd = 0;
	int hw;

	if (Getcookie(C__MCH, &mch) != C_FOUND)
		return 0;
	hw = (int)((unsigned long)mch >> 16);
	if (hw == 0)
		return 0;
	if (Getcookie(C__SND, &snd) == C_FOUND)
		return (snd & 2L) != 0L ? 1 : 0;
	return hw == 1 ? 1 : 0;
}

static void *stram_alloc(unsigned long nbytes)
{
	long p;
	/* Mxalloc needs GEMDOS >= 0.19 (Sversion 0x1900); TOS 1.6x is 0.17. */
	static int have_mxalloc = -1;

	if (have_mxalloc < 0)
		have_mxalloc = (Sversion() >= 0x1900) ? 1 : 0;

	p = have_mxalloc ? Mxalloc((long)nbytes, STVQ_MX_STRAM) : Malloc((long)nbytes);
	if (p <= 0)
		return NULL;
	return (void *)(unsigned long)p;
}

static void stram_free(void *p)
{
	if (p)
		Mfree(p);
}

static uint8_t *align256(void *raw)
{
	unsigned long a = ((unsigned long)raw + 255UL) & ~255UL;
	return (uint8_t *)a;
}

static unsigned char *align2(void *raw)
{
	unsigned long a = ((unsigned long)raw + 1UL) & ~1UL;
	return (unsigned char *)a;
}

static void set_video_base(uint8_t *phys)
{
	unsigned long p = (unsigned long)phys;
	*VID_BASE_H = (unsigned char)((p >> 16) & 0xFFU);
	*VID_BASE_M = (unsigned char)((p >> 8) & 0xFFU);
	*VID_BASE_L = (unsigned char)(p & 0xFFU);
}

static void apply_palette(const uint16_t ste[16])
{
	int i;
	for (i = 0; i < 16; i++)
		VID_PAL[i] = ste[i];
}

static void snapshot_palette(uint16_t out[16])
{
	int i;
	for (i = 0; i < 16; i++)
		out[i] = VID_PAL[i];
}

/* high_reg points at $FF8903 or $FF890F; mid/low are +2/+4. */
static void dma_set_address(volatile unsigned char *high_reg, unsigned long phys)
{
	high_reg[0] = (unsigned char)((phys >> 16) & 0xFFU);
	high_reg[2] = (unsigned char)((phys >> 8) & 0xFFU);
	high_reg[4] = (unsigned char)(phys & 0xFEU);
}

static void dma_mixer_connect(void)
{
	/* STE and TT drive the LMC1992 directly; a Falcon routes through
	 * Devconnect instead. */
	if (!ST_Hw_Is_Ste_Sound_Class())
		return;
	*STE_DMA_MIXER = 0x03;
}

static void dma_stop_impl(void)
{
	/* A Falcon keeps unrelated bits in these two registers; clear only the
	 * DMA enable, as audio_ste.cpp does. */
	if (ST_Hw_Is_Falcon_Class()) {
		*STE_DMA_CTRL &= (unsigned char)~0x03u;
		*STE_DMA_MODE &= (unsigned char)~0x03u;
	} else {
		*STE_DMA_CTRL = 0;
		*STE_DMA_MODE = 0;
	}
}

/* Arm DMA once to loop the whole ring (start/end not rewritten during play). */
static void dma_arm_loop_impl(StvqHw *hw)
{
	unsigned char mode;
	unsigned long s;
	unsigned long e;

	if (!hw->ring)
		return;

	mode = (unsigned char)(STVQ_STE_SND_MONO | hw->dma_rate_idx);
	s = (unsigned long)hw->ring;
	e = s + (unsigned long)STVQ_DMA_RING_BYTES;

	dma_stop_impl();
	dma_mixer_connect();
	*STE_DMA_SOUND_MODE = mode;
	dma_set_address(STE_DMA_START_H, s);
	dma_set_address(STE_DMA_END_H, e);
	/* $FF8901 bits 0+1: %11 = play with loop (same as audio_ste). */
	*STE_DMA_MODE = 0x03u;
	hw->ring_armed = 1;
}

/* Byte offset of current DMA fetch in ring, or -1 if outside. */
static int ring_dma_offset(StvqHw *hw)
{
	unsigned long base;
	unsigned long cnt;

	if (!hw->ring)
		return -1;
	base = (unsigned long)hw->ring;
	cnt = ((unsigned long)*STE_DMA_CNT_H << 16) | ((unsigned long)*STE_DMA_CNT_M << 8) |
	    (unsigned long)*STE_DMA_CNT_L;
	if (cnt < base || cnt >= base + (unsigned long)STVQ_DMA_RING_BYTES)
		return -1;
	return (int)(cnt - base);
}

/*
 * Account DMA progress against ring_queued. If DMA has consumed more than we
 * wrote (underrun), clamp to empty and resync the write head to the play head
 * so the next fill is immediate — modular (dma-write) free alone looks "full".
 * Call often enough that DMA cannot lap a full ring between syncs (~82 ms @ 12.5 kHz).
 */
static void ring_sync(StvqHw *hw)
{
	unsigned long base;
	unsigned long end;
	unsigned long cnt;
	unsigned dma_off;
	unsigned played;

	if (!hw->ring || !hw->ring_armed)
		return;

	base = (unsigned long)hw->ring;
	end = base + (unsigned long)STVQ_DMA_RING_BYTES;
	cnt = ((unsigned long)*STE_DMA_CNT_H << 16) | ((unsigned long)*STE_DMA_CNT_M << 8) |
	    (unsigned long)*STE_DMA_CNT_L;
	if (cnt < base || cnt >= end)
		return;

	dma_off = (unsigned)(cnt - base);

	played = (dma_off + (unsigned)STVQ_DMA_RING_BYTES - hw->ring_dma_pos) %
	    (unsigned)STVQ_DMA_RING_BYTES;
	hw->ring_dma_pos = dma_off;

	if (played >= hw->ring_queued) {
		/* Underrun: treat as empty. Keep write head even — mono DMA can sit on
		 * an odd byte, and libcmini memcpy word-copies (Address Error on odd). */
		hw->ring_queued = 0;
		hw->ring_write = dma_off & ~1u;
	} else {
		hw->ring_queued -= played;
	}
}

/*
 * Free bytes available to write (leaves 1 byte unused so write never lands on DMA).
 * When not armed, the whole usable capacity is free.
 */
static unsigned ring_free_bytes(StvqHw *hw)
{
	if (!hw->ring)
		return 0;
	if (!hw->ring_armed)
		return (unsigned)STVQ_DMA_RING_BYTES - 1u;

	ring_sync(hw);
	if (hw->ring_queued >= (unsigned)STVQ_DMA_RING_BYTES - 1u)
		return 0;
	return ((unsigned)STVQ_DMA_RING_BYTES - 1u) - hw->ring_queued;
}

/* Copy `nbytes` into the ring at write head, wrapping; advances write head. */
static void ring_write_bytes(StvqHw *hw, const unsigned char *src, unsigned nbytes)
{
	unsigned off = hw->ring_write;

	while (nbytes) {
		unsigned to_end = (unsigned)STVQ_DMA_RING_BYTES - off;
		unsigned batch = nbytes < to_end ? nbytes : to_end;

		memcpy(hw->ring + off, src, batch);
		src += batch;
		nbytes -= batch;
		off += batch;
		if (off >= (unsigned)STVQ_DMA_RING_BYTES)
			off = 0;
	}
	hw->ring_write = off;
}

/*
 * Queue `need` PCM bytes into the ring.
 * Returns 0 ok, 1 busy (not enough free), -1 error.
 */
static int ring_queue_pcm(StvqHw *hw, const unsigned char *pcm, unsigned need)
{
	unsigned freeb;
	int dma_off;

	if (!hw || !hw->ring || !pcm || need < 1)
		return -1;

	if (!hw->ring_armed) {
		/* Cold start: silence ring, place first chunk at 0, then arm loop. */
		memset(hw->ring, 0, (size_t)STVQ_DMA_RING_BYTES);
		hw->ring_write = 0;
		ring_write_bytes(hw, pcm, need);
		dma_arm_loop_impl(hw);
		hw->ring_queued = need;
		dma_off = ring_dma_offset(hw);
		hw->ring_dma_pos = dma_off >= 0 ? (unsigned)dma_off : 0;
		return 0;
	}

	freeb = ring_free_bytes(hw);
	if (freeb < need)
		return 1; /* busy -- caller should wait */

	ring_write_bytes(hw, pcm, need);
	hw->ring_queued += need;
	return 0;
}

static void clear_screen(uint8_t *s)
{
	memset(s, 0, STVQ_SCREEN_BYTES);
}

void stvq_hw_blit_tile(uint8_t *screen, unsigned px, unsigned py, const uint8_t tile32[32])
{
	unsigned row;
	unsigned group = px >> 4;
	unsigned half = (px >> 3) & 1u;
	uint8_t *base;

	if (px + 8u > STVQ_SCREEN_W || py + 8u > STVQ_SCREEN_H)
		return;

	base = screen + py * STVQ_SCREEN_PITCH + group * 8u + half;
	for (row = 0; row < 8u; row++) {
		const uint8_t *src = tile32 + row * 4u;
		uint32_t pdata = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) |
		    (uint32_t)src[3];
#if defined(__mc68000__) || defined(__mc68020__) || defined(__M68000__) || defined(__m68k__)
		__asm__ volatile("movep.l %0,0(%1)" : : "d"(pdata), "a"(base) : "memory");
#else
		base[0] = (uint8_t)(pdata >> 24);
		base[2] = (uint8_t)(pdata >> 16);
		base[4] = (uint8_t)(pdata >> 8);
		base[6] = (uint8_t)(pdata >> 0);
#endif
		base += STVQ_SCREEN_PITCH;
	}
}

int stvq_hw_init(StvqHw *hw, unsigned width, unsigned height, uint8_t *screen0, uint8_t *screen1,
    int enable_audio)
{
	int i;
	int have_screens = (screen0 != NULL && screen1 != NULL);

	memset(hw, 0, sizeof(*hw));
	hw->front = 0;
	hw->back = 1;
	hw->dma_rate_idx = STVQ_STE_RATE_12517;
	hw->screens_owned = have_screens ? 0 : 1;

	if (width == 0 || height == 0 || width > STVQ_SCREEN_W || height > STVQ_SCREEN_H)
		return -1;
	if ((screen0 == NULL) != (screen1 == NULL))
		return -1;

	hw->origin_x = (uint16_t)((STVQ_SCREEN_W - width) / 2u);
	hw->origin_y = (uint16_t)((STVQ_SCREEN_H - height) / 2u);
	hw->origin_x = (uint16_t)(hw->origin_x & ~7u);

	if (hw->screens_owned) {
		hw->old_log = (long)Logbase();
		hw->old_phys = (long)Physbase();
		hw->old_rez = Getrez();
		snapshot_palette(hw->old_pal);
	}

	hw->dma_ok = 0;
	if (enable_audio && dma_audio_available()) {
		hw->ring_raw = stram_alloc((unsigned long)STVQ_DMA_RING_BYTES + 2u);
		if (!hw->ring_raw)
			goto fail;
		hw->ring = align2(hw->ring_raw);
		memset(hw->ring, 0, (size_t)STVQ_DMA_RING_BYTES);
		hw->ring_write = 0;
		hw->ring_queued = 0;
		hw->ring_dma_pos = 0;
		hw->ring_armed = 0;
		hw->dma_ok = 1;
	}

	if (hw->screens_owned) {
		for (i = 0; i < 2; i++) {
			hw->screen_raw[i] = stram_alloc(STVQ_SCREEN_BYTES + 256u);
			if (!hw->screen_raw[i])
				goto fail;
			hw->screen[i] = align256(hw->screen_raw[i]);
			clear_screen(hw->screen[i]);
		}
	} else {
		hw->screen[0] = screen0;
		hw->screen[1] = screen1;
		clear_screen(hw->screen[0]);
		clear_screen(hw->screen[1]);
	}

	if (hw->screens_owned) {
		Setscreen((void *)-1L, (void *)-1L, 0);
		set_video_base(hw->screen[hw->front]);
		Setscreen((void *)hw->screen[hw->front], (void *)hw->screen[hw->front], -1);
		__asm__ volatile("dc.w 0xa00a"); /* hide mouse */
	} else {
		/*
		 * Game path: Setscreen page flip (log=phys=front). Keep GraphicBuffer
		 * identities; only retarget TOS Logbase/Physbase during the clip.
		 */
		Setscreen((void *)hw->screen[0], (void *)hw->screen[0], -1);
	}

	return 0;

fail:
	stvq_hw_shutdown(hw);
	return -1;
}

void stvq_hw_restore_entry_phys(StvqHw *hw)
{
	if (!hw || !hw->screen[0])
		return;
	hw->front = 0;
	hw->back = 1;
	if (hw->screens_owned)
		set_video_base(hw->screen[0]);
	else
		Setscreen((void *)hw->screen[0], (void *)hw->screen[0], -1);
}

void stvq_hw_shutdown(StvqHw *hw)
{
	int i;

	if (!hw)
		return;

	stvq_hw_pcm_stop(hw);

	/* Always put display back on entry Visible (screen[0]) before releasing. */
	if (hw->screen[0]) {
		hw->front = 0;
		hw->back = 1;
		if (hw->screens_owned)
			set_video_base(hw->screen[0]);
		else
			Setscreen((void *)hw->screen[0], (void *)hw->screen[0], -1);
	}

	if (hw->screens_owned) {
		__asm__ volatile("dc.w 0xa009"); /* show mouse */

		apply_palette(hw->old_pal);
		Setscreen((void *)hw->old_log, (void *)hw->old_phys, hw->old_rez);

		for (i = 0; i < 2; i++) {
			stram_free(hw->screen_raw[i]);
			hw->screen_raw[i] = NULL;
			hw->screen[i] = NULL;
		}
	} else {
		hw->screen[0] = NULL;
		hw->screen[1] = NULL;
	}

	stram_free(hw->ring_raw);
	hw->ring_raw = NULL;
	hw->ring = NULL;
}

uint8_t *stvq_hw_back(StvqHw *hw)
{
	return hw->screen[hw->back];
}

uint8_t *stvq_hw_front(StvqHw *hw)
{
	return hw->screen[hw->front];
}

void stvq_hw_set_pending_palette(StvqHw *hw, const uint16_t ste_be[16])
{
	int i;
	for (i = 0; i < 16; i++)
		hw->pending_pal[i] = ste_be[i];
	hw->pending_pal_valid = 1;
}

void stvq_hw_present_begin(StvqHw *hw)
{
	int new_front = hw->back;

	/*
	 * Queue phys (+ optional palette) for the next VBL.
	 * Caller may do useful work (e.g. next-frame disk read) before present_end.
	 * pending_pal must remain valid until present_end (through the applying VBL).
	 *
	 * Record _vbclock so present_end can skip Vsync when that VBL already
	 * ran during the overlapped work (otherwise Vsync waits for yet another).
	 */
	hw->present_new_front = new_front;
	hw->present_vbl0 = *(volatile unsigned long *)0x462UL;
	if (hw->pending_pal_valid)
		Setpalette(hw->pending_pal);
	Setscreen((void *)hw->screen[new_front], (void *)hw->screen[new_front], -1);
}

unsigned long stvq_hw_present_end(StvqHw *hw)
{
	int new_front = hw->present_new_front;
	unsigned long t0, t1;

	t0 = stvq_hz200();
	if (*(volatile unsigned long *)0x462UL == hw->present_vbl0)
		Vsync();
	if (hw->pending_pal_valid) {
		/* TOS Setpalette leaves colorptr ($45A) armed; clear so later
		 * pending_pal edits are not live-copied on pacing Vsyncs. */
		*(volatile unsigned long *)0x45AUL = 0;
		hw->pending_pal_valid = 0;
	}
	hw->front = new_front;
	hw->back = 1 - new_front;
	t1 = stvq_hz200();
	return t1 - t0;
}

unsigned long stvq_hw_present(StvqHw *hw)
{
	stvq_hw_present_begin(hw);
	return stvq_hw_present_end(hw);
}

static unsigned char rate_idx_for(unsigned sample_rate)
{
	if (sample_rate >= 20000u)
		return (unsigned char)STVQ_STE_RATE_25033;
	return (unsigned char)STVQ_STE_RATE_12517;
}

static int pcm_prepare(StvqHw *hw, const unsigned char **pcm, size_t *len, unsigned sample_rate)
{
	if (!hw->dma_ok || !hw->ring || !*pcm || *len < 1)
		return -1;

	if (!sample_rate)
		sample_rate = STVQ_SAMPLE_RATE;
	hw->dma_rate_idx = rate_idx_for(sample_rate);

	if (*len > (size_t)STVQ_DMA_RING_BYTES - 1u)
		*len = (size_t)STVQ_DMA_RING_BYTES - 1u;
	*len &= ~1u; /* keep ring_write even */
	if (*len < 1)
		return -1;
	return 0;
}

void stvq_hw_pcm_start(StvqHw *hw, const unsigned char *pcm, size_t len, unsigned sample_rate)
{
	int rc;

	if (pcm_prepare(hw, &pcm, &len, sample_rate) != 0)
		return;

	for (;;) {
		rc = ring_queue_pcm(hw, pcm, (unsigned)len);
		if (rc != 1)
			break;
	}
}

int stvq_hw_pcm_busy(StvqHw *hw, size_t need)
{
	unsigned freeb;

	if (!hw->dma_ok || !hw->ring)
		return 0;
	if (!hw->ring_armed)
		return 0; /* cold start accepts immediately */

	if (need < 1)
		return 0;
	if (need > (size_t)STVQ_DMA_RING_BYTES - 1u)
		need = (size_t)STVQ_DMA_RING_BYTES - 1u;

	freeb = ring_free_bytes(hw);
	return freeb < need ? 1 : 0;
}

void stvq_hw_pcm_stop(StvqHw *hw)
{
	if (hw->dma_ok) {
		dma_stop_impl();
		hw->ring_armed = 0;
		if (hw->ring)
			memset(hw->ring, 0, (size_t)STVQ_DMA_RING_BYTES);
		hw->ring_write = 0;
		hw->ring_queued = 0;
		hw->ring_dma_pos = 0;
	}
}

void stvq_hw_wait_vbl(StvqHw *hw)
{
	(void)hw;
	Vsync();
}

int stvq_hw_poll_key(void)
{
	if (Bconstat(2) == 0)
		return 0;
	return (int)(Bconin(2) & 0xFF);
}
