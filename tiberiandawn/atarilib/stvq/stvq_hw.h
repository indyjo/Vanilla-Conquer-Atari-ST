/*
 * stvq_hw.h - LoRes ping-pong screens, VBL-synced palette, STE DMA ring audio.
 */
#ifndef STVQ_HW_H
#define STVQ_HW_H

#include "stvq_format.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Looping DMA ring in ST-RAM (~82 ms @ 12517 Hz). */
enum { STVQ_DMA_RING_BYTES = 1024 };

typedef struct StvqHw {
	void *screen_raw[2]; /* Mxalloc blocks (for free); NULL if caller-supplied */
	uint8_t *screen[2];  /* planar 320x200 (aligned when owned) */
	int screens_owned;   /* 1 = allocated here; 0 = caller buffers */
	int front;           /* index currently displayed */
	int back;            /* index being drawn into */

	uint16_t origin_x; /* pixel offset of tile (0,0) */
	uint16_t origin_y;

	int dma_ok; /* STE/TT/Falcon DMA or YM/Covox timer ring available and audio enabled */
	int timer_mode; /* 1 = DigiRing via Timer A (YM/Covox); 0 = STE DMA */

	/* Present: palette queued for Setpalette on next present/Vsync. */
	volatile int pending_pal_valid;
	uint16_t pending_pal[16];

	/* Saved TOS state (owned-screen / standalone mode only). */
	long old_log;
	long old_phys;
	short old_rez;
	uint16_t old_pal[16];

	/* Looping STE DMA ring (ST-RAM). */
	unsigned char *ring_raw;
	unsigned char *ring; /* even-aligned */
	unsigned ring_write; /* next write offset 0 .. STVQ_DMA_RING_BYTES-1 */
	unsigned ring_queued; /* valid bytes ahead of DMA (software; detects underrun) */
	unsigned ring_dma_pos; /* last DMA offset observed by ring_sync */
	int ring_armed;      /* DMA looping */
	unsigned char dma_rate_idx; /* STE sound-mode rate bits */

	/* present_begin / present_end */
	int present_new_front;
	unsigned long present_vbl0; /* _vbclock at begin; skip Vsync if it advanced */
} StvqHw;

/*
 * screen0/screen1: if both non-NULL, use caller planar ST-RAM buffers (game
 * visible page + backplane page; HiddenPage may alias screen1). Game path flips
 * with Setscreen(log=phys=front) then Vsync. If both NULL, allocate ST-RAM
 * screens (standalone stvqview).
 * enable_audio: non-zero to allocate/use DMA ring when hardware supports it.
 */
int stvq_hw_init(StvqHw *hw, unsigned width, unsigned height, uint8_t *screen0, uint8_t *screen1,
    int enable_audio);
void stvq_hw_shutdown(StvqHw *hw);

/* Force phys base back to screen[0] (entry VisiblePage). Safe any time after init. */
void stvq_hw_restore_entry_phys(StvqHw *hw);

uint8_t *stvq_hw_back(StvqHw *hw);
uint8_t *stvq_hw_front(StvqHw *hw);

/* Queue STPL for the next present (Setpalette on reveal VBL; colorptr cleared after). */
void stvq_hw_set_pending_palette(StvqHw *hw, const uint16_t ste_be[16]);

/* Queue swap+palette for next VBL (Setscreen); does not wait. Pair with present_end.
 * Snapshots _vbclock so present_end can skip Vsync if that VBL already ran
 * during overlapped work (avoids waiting an extra frame). */
void stvq_hw_present_begin(StvqHw *hw);

/* Wait until queued present has taken effect. Vsync only if _vbclock unchanged. */
unsigned long stvq_hw_present_end(StvqHw *hw);

/* present_begin + present_end. Returns _hz200 ticks spent in present_end's Vsync. */
unsigned long stvq_hw_present(StvqHw *hw);

/*
 * Copy signed-8 PCM into the looping DMA ring (wraps; never overtakes DMA).
 * Cold-arms the ring on first call. sample_rate selects STE rate (12517/25033).
 * `pcm` need only stay valid until this returns (CPU memcpy into ST-RAM ring).
 */
void stvq_hw_pcm_start(StvqHw *hw, const unsigned char *pcm, size_t len, unsigned sample_rate);

/*
 * Audio master clock: 1 while the ring has fewer than `need` free bytes
 * (cannot submit the next chunk yet without overtaking DMA).
 * Free space uses a software queued count so DMA underrun (DMA lapping the
 * write cursor) is treated as empty — refill immediately instead of waiting
 * on the inverted modular (dma-write) gap.
 */
int stvq_hw_pcm_busy(StvqHw *hw, size_t need);

void stvq_hw_pcm_stop(StvqHw *hw);

/* Wait one VBL (via present machinery / Vsync fallback). */
void stvq_hw_wait_vbl(StvqHw *hw);

/* Poll keyboard: returns ASCII or 0. Scans IKBD via Bconin when ready. */
int stvq_hw_poll_key(void);

/*
 * Blit one 8x8 codebook tile (32 bytes, movep order) to planar dest.
 * dest = first plane-0 byte of the top row (movep column).
 */
static inline void stvq_movep_tile(uint8_t *dest, const uint8_t *tile32)
{
	uint32_t t;
#if defined(__mc68000__) || defined(__mc68020__) || defined(__M68000__) || defined(__m68k__)
	__asm__ __volatile__(
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,0(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,160(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,320(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,480(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,640(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,800(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,960(%2)\n\t"
	    "move.l (%1)+,%0\n\t"
	    "movep.l %0,1120(%2)"
	    : "=&d"(t), "+a"(tile32)
	    : "a"(dest)
	    : "memory");
#else
	{
		unsigned row;
		for (row = 0; row < 8u; row++) {
			dest[0] = tile32[0];
			dest[2] = tile32[1];
			dest[4] = tile32[2];
			dest[6] = tile32[3];
			tile32 += 4;
			dest += STVQ_SCREEN_PITCH;
		}
	}
	(void)t;
#endif
}

/* Screen address of 8x8 tile at pixel (px,py); px must be multiple of 8. */
static inline uint8_t *stvq_tile_dest(uint8_t *screen, unsigned px, unsigned py)
{
	return screen + py * STVQ_SCREEN_PITCH + (px >> 4) * 8u + ((px >> 3) & 1u);
}

void stvq_hw_blit_tile(uint8_t *screen, unsigned px, unsigned py, const uint8_t tile32[32]);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_HW_H */
