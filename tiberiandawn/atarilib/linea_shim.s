	.globl ___aline
	.globl ___fonts
	.globl ___funcs
	.globl _linea0

	.bss
	.even
___funcs:
	.ds.l 1
___fonts:
	.ds.l 1
___aline:
	.ds.l 1

	.text
	.even
_linea0:
	movel %a2,%sp@-
	.short 0xa000
	movel %a0,___aline
	movel %a1,___fonts
	movel %a2,___funcs
	moveal %sp@+,%a2
	rts
