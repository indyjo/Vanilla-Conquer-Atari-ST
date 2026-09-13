/* Debug HBL: occupy ~95% of an 8 MHz PAL scanline (512 cycles), then rte.
 *
 * Autovector entry 44 + move.w d0,-(sp) 8 + move.w #imm,d0 8
 * + dbra 10*N+14 + move.w (sp)+,d0 8 + rte 20.
 * N=38 => 482 cycles (~94% of 512). Next HBL cannot nest (IPL 2).
 */
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a##b
#define SYM(x) CONCAT1(__USER_LABEL_PREFIX__, x)

	.cpu	68000
	.text
	.align	2

	.globl	SYM(ST_Slow_Hbl_Isr)
	.type	SYM(ST_Slow_Hbl_Isr), @function
SYM(ST_Slow_Hbl_Isr):
	move.w	%d0,-(%sp)
	move.w	#0x1c,%d0
1:	dbra	%d0,1b
	move.w	(%sp)+,%d0
	rte
	.size	SYM(ST_Slow_Hbl_Isr), .-SYM(ST_Slow_Hbl_Isr)
