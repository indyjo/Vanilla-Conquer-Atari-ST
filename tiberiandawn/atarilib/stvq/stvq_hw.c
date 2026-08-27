/*
 * stvq_hw.c - Ping-pong LoRes, Setscreen/Setpalette+Vsync flip, digi via Digi_*.
 *
 * Audio is process-wide Digi HAL only (no private DMA ring). Caller must have
 * Digi_Submit installed (Audio_Init) before enable_audio. Yield/Reclaim in the
 * game path pauses the mixer VBL around playback.
 *
 * Caller must be in supervisor mode (game startup / stvqview Super(0)).
 */
#include "stvq_hw.h"

#include "audio/digi_audio.h"
#include "stvq_prof.h"

#include <mint/osbind.h>

#include <string.h>

enum { STVQ_MX_STRAM = 0 };

static volatile unsigned char *const VID_BASE_H = (volatile unsigned char *)0xFF8201UL;
static volatile unsigned char *const VID_BASE_M = (volatile unsigned char *)0xFF8203UL;
static volatile unsigned char *const VID_BASE_L = (volatile unsigned char *)0xFF820DUL;
static volatile uint16_t *const VID_PAL = (volatile uint16_t *)0xFF8240UL;

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

static void clear_screen(uint8_t *s)
{
	memset(s, 0, STVQ_SCREEN_BYTES);
}

static unsigned digi_src_rate_flags(void)
{
	return DIGI_RATE_12500;
}

static unsigned digi_ring_cap(void)
{
	if (Digi_Info && Digi_Info()->ring_samples > 1u)
		return Digi_Info()->ring_samples - 1u;
	return 1023u;
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
	if (enable_audio && Digi_Submit && Digi_Capacity)
		hw->dma_ok = 1;

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

void stvq_hw_pcm_start(StvqHw *hw, const unsigned char *pcm, size_t len, unsigned sample_rate)
{
	unsigned char const *s;
	unsigned left;
	unsigned cap;

	(void)sample_rate;
	if (!hw || !hw->dma_ok || !pcm || len < 1 || !Digi_Submit)
		return;

	cap = digi_ring_cap();
	if (len > (size_t)cap)
		len = (size_t)cap;
	len &= ~1u;
	if (len < 1)
		return;

	s = pcm;
	left = (unsigned)len;
	while (left > 0) {
		unsigned freeb;
		unsigned chunk;
		void const *p;
		unsigned got;

		if (!Digi_Capacity)
			return;
		freeb = Digi_Capacity(digi_src_rate_flags());
		if (freeb == 0)
			continue; /* spin until HAL drains */
		chunk = left < freeb ? left : freeb;
		chunk &= ~1u;
		if (chunk == 0)
			continue;
		p = Digi_Submit(s, s + chunk, digi_src_rate_flags());
		got = (unsigned)((unsigned char const *)p - s);
		if (got == 0)
			continue;
		s += got;
		left -= got;
	}
}

int stvq_hw_pcm_busy(StvqHw *hw, size_t need)
{
	unsigned freeb;

	if (!hw || !hw->dma_ok || !Digi_Capacity)
		return 0;
	if (need < 1)
		return 0;
	freeb = Digi_Capacity(digi_src_rate_flags());
	return freeb < need ? 1 : 0;
}

void stvq_hw_pcm_stop(StvqHw *hw)
{
	if (!hw || !hw->dma_ok)
		return;
	if (Digi_Flush)
		Digi_Flush();
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
