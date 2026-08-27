/* YM Timer A digi ISR + 64-entry 2-ch movep LUT (PC-relative).
 * Ring bytes are Digi_Submit offsets (s&$FC). LUT is signed-aware
 * (ym_lut --mode grid --pcm signed: silence/index0 = mid amplitude).
 * Assembled via CPP for label prefix.
 */
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a##b
#define SYM(x) CONCAT1(__USER_LABEL_PREFIX__, x)

	.cpu	68000
	.text
	.align	2

	.globl	SYM(timer_a_ym_s1)
	.type	SYM(timer_a_ym_s1), @function
SYM(timer_a_ym_s1):
	movem.l	%d0-%d1/%a0,-(%sp)
	move.l	%usp,%a0			| 2048-aligned base | 10-bit offset
	moveq	#0,%d1
	move.b	(%a0)+,%d1			| native LUT byte offset; advance a0
	move.l	%a0,%d0				| keep updated pointer for wrap/store
	andi.w	#0xFBFF,%d0			| make the cursor wrap every 1024 samples
	lea	ym_lut(%pc),%a0			| load address of table into a0
	move.l	(0,%a0,%d1.w),%d1		| read 4 bytes from LUT
	lea	0xFFFF8800.w,%a0			| this is the soundchip address
	movep.l	%d1,0(%a0)			| ... and write into soundchip
	move.l	%d0,%a0				| USP may only be written from An
	move.l	%a0,%usp			| write-back play pointer
	move.b	#0xDF,0xFFFFFA0F.w		| clear Timer A in-service (ISRA)
	movem.l	(%sp)+,%d0-%d1/%a0
	rte
	.size	SYM(timer_a_ym_s1), .-SYM(timer_a_ym_s1)

	.align	2
ym_lut:
#include "ym_grid_2ch_6bit_lut.S.inc"