/*
 * ST hardware blitter backend ($FF8A20).
 */

#include "st_blit.h"

#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(void *) == 4, "ST_Blitter address fields require 32-bit pointers");
static_assert(sizeof(ST_Blitter) == 30, "ST_Blitter must match $FF8A20..$FF8A3D");
static_assert(offsetof(ST_Blitter, src_x_inc) == 0, "ST_Blitter src_x_inc offset");
static_assert(offsetof(ST_Blitter, src_y_inc) == 2, "ST_Blitter src_y_inc offset");
static_assert(offsetof(ST_Blitter, src_addr) == 4, "ST_Blitter src_addr offset");
static_assert(offsetof(ST_Blitter, endmask1) == 8, "ST_Blitter endmask1 offset");
static_assert(offsetof(ST_Blitter, endmask2) == 10, "ST_Blitter endmask2 offset");
static_assert(offsetof(ST_Blitter, endmask3) == 12, "ST_Blitter endmask3 offset");
static_assert(offsetof(ST_Blitter, dst_x_inc) == 14, "ST_Blitter dst_x_inc offset");
static_assert(offsetof(ST_Blitter, dst_y_inc) == 16, "ST_Blitter dst_y_inc offset");
static_assert(offsetof(ST_Blitter, dst_addr) == 18, "ST_Blitter dst_addr offset");
static_assert(offsetof(ST_Blitter, x_count) == 22, "ST_Blitter x_count offset");
static_assert(offsetof(ST_Blitter, y_count) == 24, "ST_Blitter y_count offset");
static_assert(offsetof(ST_Blitter, hop) == 26, "ST_Blitter hop offset");
static_assert(offsetof(ST_Blitter, op) == 27, "ST_Blitter op offset");
static_assert(offsetof(ST_Blitter, ctrl) == 28, "ST_Blitter ctrl offset");
static_assert(offsetof(ST_Blitter, skew) == 29, "ST_Blitter skew offset");

#define g_Blitter (*(volatile ST_Blitter *)0xFFFF8A20UL)

/*
 * Shared mode only, called after BUSY has been set. Shared mode hands the bus
 * back often enough for interrupts. Re-asserting BUSY makes the chip resume
 * after ~7 cycles instead of a full 64-cycle slice. The bset that observes
 * BUSY clear also sets it, so it starts one more blit; y_count is already 0.
 * The following read wait lets that init finish and does not start another.
 */
static void ST_Blitter_Finish_Shared(void)
{
	volatile uint8_t *const ctrl = &g_Blitter.ctrl;
#if defined(__m68k__)
	__asm__ volatile(
		"1:\n\t"
		"bset.b #7,(%0)\n\t"
		"nop\n\t"
		"bne.s 1b"
		:
		: "a"(ctrl)
		: "cc", "memory");
#else
	for (;;) {
		const uint8_t prev = *ctrl;
		*ctrl = (uint8_t)(prev | 0x80u);
		if ((prev & 0x80u) == 0) {
			break;
		}
	}
#endif
	while ((g_Blitter.ctrl & 0x80u) != 0) {
	}
}

static ST_Blitter_Backend g_hw_backend;

ST_Blitter_Backend::ST_Blitter_Backend() : ST_Blit_Backend(g_Blitter)
{
}

ST_Blitter_Backend &ST_Blit_HW_Backend()
{
	return g_hw_backend;
}

void ST_Blitter_Backend::Execute(bool hog, uint16_t lines, void *src_addr, void *dst_addr)
{
	if (lines == 0) {
		return;
	}
	g_Blitter.src_addr = src_addr;
	g_Blitter.dst_addr = dst_addr;
	g_Blitter.y_count = lines;
	if (hog) {
		/* HOG stalls the CPU until this plane finishes. The chip is idle on return. */
		g_Blitter.ctrl = ST_BLIT_CTRL_START_HOG;
		return;
	}
	g_Blitter.ctrl = ST_BLIT_CTRL_START;
	ST_Blitter_Finish_Shared();
}
