/*
 * stvq_hw.c - Ping-pong LoRes, VBL palette/swap, STE DMA looping ring audio.
 *
 * Audio matches audio_ste.cpp: fixed ST-RAM ring, arm once with loop, mixer poke
 * at $FFFF8922, write head that never overtakes the DMA frame counter.
 *
 * Stay in user mode; use Supexec only for privileged register / VBL-queue access.
 */
#include "stvq_hw.h"
#include "stvq_prof.h"

#include <mint/cookie.h>
#include <mint/osbind.h>
#include <mint/sysvars.h>

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

static StvqHw *g_hw;

static volatile unsigned char *const VID_BASE_H = (volatile unsigned char *)0xFF8201UL;
static volatile unsigned char *const VID_BASE_M = (volatile unsigned char *)0xFF8203UL;
static volatile unsigned char *const VID_BASE_L = (volatile unsigned char *)0xFF820DUL;
static volatile uint16_t *const VID_PAL = (volatile uint16_t *)0xFF8240UL;

/* Supexec argument marshalling. */
static uint8_t *g_sup_phys;
static const uint16_t *g_sup_pal;
static const unsigned char *g_sup_pcm;
static size_t g_sup_pcm_len;
static long g_sup_rc;

static void stvq_vbl_proc(void);

static void (**vbl_queue_table(void))(void)
{
	return (void (**)(void)) * (unsigned long *)0x456UL;
}

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

/* high_reg points at $FF8903 or $FF890F; mid/low are +2/+4. */
static void dma_set_address(volatile unsigned char *high_reg, unsigned long phys)
{
	high_reg[0] = (unsigned char)((phys >> 16) & 0xFFU);
	high_reg[2] = (unsigned char)((phys >> 8) & 0xFFU);
	high_reg[4] = (unsigned char)(phys & 0xFEU);
}

static void dma_mixer_connect(void)
{
	*STE_DMA_MIXER = 0x03;
}

static void dma_stop_impl(void)
{
	*STE_DMA_CTRL = 0;
	*STE_DMA_MODE = 0;
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
 * Free bytes from write head forward to DMA read (exclusive).
 * When not armed, the whole ring is free.
 * Write head may be odd; only the DMA frame start/end need even addresses.
 */
static unsigned ring_free_bytes(StvqHw *hw)
{
	int dma_off;
	unsigned space;

	if (!hw->ring)
		return 0;
	if (!hw->ring_armed)
		return (unsigned)STVQ_DMA_RING_BYTES;

	dma_off = ring_dma_offset(hw);
	if (dma_off < 0)
		return 0;

	space = ((unsigned)dma_off + (unsigned)STVQ_DMA_RING_BYTES - hw->ring_write) %
	    (unsigned)STVQ_DMA_RING_BYTES;
	return space;
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
 * Zero the free region (write head -> DMA) without advancing the write head.
 * Prevents DMA from replaying stale samples when it catches up or when we only
 * partially refill free space -- that was a periodic click in quiet sections.
 */
static void ring_scrub_free(StvqHw *hw)
{
	unsigned freeb;
	unsigned off;

	if (!hw->ring || !hw->ring_armed)
		return;

	freeb = ring_free_bytes(hw);
	off = hw->ring_write;
	while (freeb) {
		unsigned to_end = (unsigned)STVQ_DMA_RING_BYTES - off;
		unsigned batch = freeb < to_end ? freeb : to_end;

		memset(hw->ring + off, 0, batch);
		freeb -= batch;
		off += batch;
		if (off >= (unsigned)STVQ_DMA_RING_BYTES)
			off = 0;
	}
}

static long sup_set_video_base(void)
{
	set_video_base(g_sup_phys);
	return 0;
}

static long sup_apply_palette(void)
{
	apply_palette(g_sup_pal);
	return 0;
}

static long sup_snapshot_palette(void)
{
	StvqHw *hw = g_hw;
	int i;
	for (i = 0; i < 16; i++)
		hw->old_pal[i] = VID_PAL[i];
	return 0;
}

static long sup_dma_stop(void)
{
	dma_stop_impl();
	if (g_hw)
		g_hw->ring_armed = 0;
	return 0;
}

static long sup_ring_free(void)
{
	ring_scrub_free(g_hw);
	return (long)ring_free_bytes(g_hw);
}

static long sup_ring_write(void)
{
	StvqHw *hw = g_hw;
	unsigned need = (unsigned)g_sup_pcm_len;
	unsigned freeb;

	if (!hw || !hw->ring || !g_sup_pcm || need < 1)
		return -1;

	if (!hw->ring_armed) {
		/* Cold start: silence ring, place first chunk at 0, then arm loop. */
		memset(hw->ring, 0, (size_t)STVQ_DMA_RING_BYTES);
		hw->ring_write = 0;
		ring_write_bytes(hw, g_sup_pcm, need);
		dma_arm_loop_impl(hw);
		ring_scrub_free(hw);
		return 0;
	}

	ring_scrub_free(hw);
	freeb = ring_free_bytes(hw);
	if (freeb < need)
		return 1; /* busy -- caller should wait */

	ring_write_bytes(hw, g_sup_pcm, need);
	ring_scrub_free(hw);
	return 0;
}

static long sup_vbl_install(void)
{
	StvqHw *hw = g_hw;
	short n;
	void (**vq)(void);
	int i;
	int first_free_found = 0;

	hw->vbl_slot = -1;
	n = *nvbls;
	if (n <= 0) {
		g_sup_rc = -1;
		return 0;
	}
	vq = vbl_queue_table();
	for (i = 0; i < n; i++) {
		if (vq[i] == 0) {
			if (!first_free_found) {
				first_free_found = 1;
				continue;
			}
			vq[i] = stvq_vbl_proc;
			hw->vbl_slot = i;
			g_sup_rc = 0;
			return 0;
		}
	}
	g_sup_rc = -1;
	return 0;
}

static long sup_vbl_remove(void)
{
	StvqHw *hw = g_hw;
	if (!hw || hw->vbl_slot < 0)
		return 0;
	{
		short n = *nvbls;
		void (**vq)(void) = vbl_queue_table();
		if (hw->vbl_slot < n)
			vq[hw->vbl_slot] = 0;
	}
	hw->vbl_slot = -1;
	return 0;
}

static void stvq_vbl_proc(void)
{
	StvqHw *hw = g_hw;
	if (!hw || !hw->present_req)
		return;

	if (hw->pending_pal_valid) {
		apply_palette(hw->pending_pal);
		hw->pending_pal_valid = 0;
	}
	if (hw->pending_phys)
		set_video_base((uint8_t *)hw->pending_phys);

	hw->present_req = 0;
	hw->present_done = 1;
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

int stvq_hw_init(StvqHw *hw, unsigned width, unsigned height)
{
	int i;

	memset(hw, 0, sizeof(*hw));
	hw->front = 0;
	hw->back = 1;
	hw->vbl_slot = -1;
	hw->super_stack = 0;
	hw->dma_rate_idx = STVQ_STE_RATE_12517;

	if (width == 0 || height == 0 || width > STVQ_SCREEN_W || height > STVQ_SCREEN_H)
		return -1;

	hw->origin_x = (uint16_t)((STVQ_SCREEN_W - width) / 2u);
	hw->origin_y = (uint16_t)((STVQ_SCREEN_H - height) / 2u);
	hw->origin_x = (uint16_t)(hw->origin_x & ~7u);

	hw->old_log = (long)Logbase();
	hw->old_phys = (long)Physbase();
	hw->old_rez = Getrez();

	g_hw = hw;
	Supexec(sup_snapshot_palette);

	hw->dma_ok = dma_audio_available();
	if (hw->dma_ok) {
		hw->ring_raw = stram_alloc((unsigned long)STVQ_DMA_RING_BYTES + 2u);
		if (!hw->ring_raw)
			goto fail;
		hw->ring = align2(hw->ring_raw);
		memset(hw->ring, 0, (size_t)STVQ_DMA_RING_BYTES);
		hw->ring_write = 0;
		hw->ring_armed = 0;
	}

	for (i = 0; i < 2; i++) {
		hw->screen_raw[i] = stram_alloc(STVQ_SCREEN_BYTES + 256u);
		if (!hw->screen_raw[i])
			goto fail;
		hw->screen[i] = align256(hw->screen_raw[i]);
		clear_screen(hw->screen[i]);
	}

	g_sup_rc = -1;
	Supexec(sup_vbl_install);
	if (g_sup_rc != 0)
		goto fail;

	Setscreen((void *)-1L, (void *)-1L, 0);
	g_sup_phys = hw->screen[hw->front];
	Supexec(sup_set_video_base);
	Setscreen((void *)hw->screen[hw->front], (void *)hw->screen[hw->front], -1);

	__asm__ volatile("dc.w 0xa00a"); /* hide mouse */

	return 0;

fail:
	stvq_hw_shutdown(hw);
	return -1;
}

void stvq_hw_shutdown(StvqHw *hw)
{
	int i;

	if (!hw)
		return;

	stvq_hw_pcm_stop(hw);

	g_hw = hw;
	Supexec(sup_vbl_remove);
	g_hw = NULL;

	__asm__ volatile("dc.w 0xa009"); /* show mouse */

	g_sup_pal = hw->old_pal;
	Supexec(sup_apply_palette);
	Setscreen((void *)hw->old_log, (void *)hw->old_phys, hw->old_rez);

	for (i = 0; i < 2; i++) {
		stram_free(hw->screen_raw[i]);
		hw->screen_raw[i] = NULL;
		hw->screen[i] = NULL;
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

unsigned long stvq_hw_present(StvqHw *hw)
{
	int new_front = hw->back;
	unsigned long t0, t1;

	while (hw->present_req)
		;

	hw->present_done = 0;
	hw->pending_phys = hw->screen[new_front];
	hw->present_req = 1;

	t0 = stvq_hz200();
	while (!hw->present_done)
		;
	t1 = stvq_hz200();

	hw->front = new_front;
	hw->back = 1 - new_front;
	return t1 - t0;
}

static unsigned char rate_idx_for(unsigned sample_rate)
{
	if (sample_rate >= 20000u)
		return (unsigned char)STVQ_STE_RATE_25033;
	return (unsigned char)STVQ_STE_RATE_12517;
}

void stvq_hw_pcm_start(StvqHw *hw, const unsigned char *pcm, size_t len, unsigned sample_rate)
{
	long rc;

	if (!hw->dma_ok || !hw->ring || !pcm || len < 1)
		return;

	if (!sample_rate)
		sample_rate = STVQ_SAMPLE_RATE;
	hw->dma_rate_idx = rate_idx_for(sample_rate);

	if (len > (size_t)STVQ_DMA_RING_BYTES)
		len = (size_t)STVQ_DMA_RING_BYTES;

	g_hw = hw;
	g_sup_pcm = pcm;
	g_sup_pcm_len = len;

	/* Wait in user mode until the ring has room (never overtake DMA). */
	for (;;) {
		rc = Supexec(sup_ring_write);
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
	if (need > (size_t)STVQ_DMA_RING_BYTES)
		need = (size_t)STVQ_DMA_RING_BYTES;

	g_hw = hw;
	freeb = (unsigned)Supexec(sup_ring_free);
	return freeb < need ? 1 : 0;
}

void stvq_hw_pcm_stop(StvqHw *hw)
{
	if (hw->dma_ok) {
		g_hw = hw;
		Supexec(sup_dma_stop);
		if (hw->ring)
			memset(hw->ring, 0, (size_t)STVQ_DMA_RING_BYTES);
		hw->ring_write = 0;
		hw->ring_armed = 0;
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
