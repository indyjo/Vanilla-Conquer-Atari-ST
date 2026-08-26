/* LINE-A init shim for libcmini (mintlib already provides linea0).
 * Assembled via CPP (.S) so __USER_LABEL_PREFIX__ matches a.out vs mintelf.
 */
#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a##b
#define SYM(x) CONCAT1(__USER_LABEL_PREFIX__, x)

	.globl SYM(__aline)
	.globl SYM(__fonts)
	.globl SYM(__funcs)
	.globl SYM(linea0)

	.bss
	.even
SYM(__funcs):
	.ds.l 1
SYM(__fonts):
	.ds.l 1
SYM(__aline):
	.ds.l 1

	.text
	.even
SYM(linea0):
	movel %a2,%sp@-
	.short 0xa000
	movel %a0,SYM(__aline)
	movel %a1,SYM(__fonts)
	movel %a2,SYM(__funcs)
	moveal %sp@+,%a2
	rts
