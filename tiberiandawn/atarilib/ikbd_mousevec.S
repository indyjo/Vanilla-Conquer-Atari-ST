/* TOS mousevec trampoline for USB (and any other software) IKBD packets.
 * Assembled via CPP (.S) so __USER_LABEL_PREFIX__ matches a.out vs mintelf.
 *
 * TOS ABI: A0 -> 3-byte relative packet ($F8..$FB, dx, dy). USB4TOS/FreeMiNT
 * mouse.udd calls this through KBDVECS.mousevec (send_packet).
 *
 * The C hook is built with the game's -mfastcall (pointer already in A0) and
 * also gets the pointer on the stack so FASTCALL=0 cdecl still works.
 *
 * XBRA id 'CNCM'. IKBD_PrevMousevec is the XBRA "next" slot (handler-4).
 */
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a##b
#define SYM(x) CONCAT1(__USER_LABEL_PREFIX__, x)

	.globl SYM(IKBD_Mousevec_Entry)
	.globl SYM(IKBD_Mousevec_C)
	.globl SYM(IKBD_PrevMousevec)

	.text
	.even
	.dc.l 0x58425241			| 'XBRA'
	.dc.l 0x434e434d			| 'CNCM'
SYM(IKBD_PrevMousevec):
	.dc.l 0
SYM(IKBD_Mousevec_Entry):
	moveml %d0-%d7/%a0-%a6,%sp@-
	movel %a0,%sp@-
	jbsr SYM(IKBD_Mousevec_C)
	addql #4,%sp
	moveml %sp@+,%d0-%d7/%a0-%a6
	movel SYM(IKBD_PrevMousevec),%a1
	cmpal #0,%a1
	beqs 1f
	jmp %a1@
1:	rts
