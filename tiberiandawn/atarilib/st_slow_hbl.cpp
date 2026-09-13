/*
 * st_slow_hbl.cpp - install/remove the debug HBL slowdown ISR.
 */

#include "st_slow_hbl.h"

#include <mint/osbind.h>

enum {
	HBL_VECTOR_NUM = 26 /* autovector level 2 at $68 */
};

static void (*Old_Hbl)(void) = 0;
static unsigned short Saved_Sr = 0;
static int Installed = 0;

static unsigned short read_sr(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0" : "=d"(sr));
	return sr;
}

static void write_sr(unsigned short sr)
{
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory", "cc");
}

void ST_Slow_Hbl_Toggle(void)
{
	if (Installed) {
		ST_Slow_Hbl_Remove();
		return;
	}

	Old_Hbl = (void (*)(void))Setexc(HBL_VECTOR_NUM, (void (*)())ST_Slow_Hbl_Isr);
	Saved_Sr = read_sr();
	/* IPL 1: HBL (2) is accepted; VBL (4) and MFP (6) still nest. */
	write_sr((unsigned short)((Saved_Sr & 0xF8FFu) | 0x0100u));
	Installed = 1;
}

void ST_Slow_Hbl_Remove(void)
{
	if (!Installed) {
		return;
	}
	write_sr(Saved_Sr);
	Setexc(HBL_VECTOR_NUM, (void (*)())Old_Hbl);
	Old_Hbl = 0;
	Installed = 0;
}
