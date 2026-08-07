/*
 * st_hw_probe.cpp — TOS cookie-jar probes for DMA-capable Atari hardware.
 */

#include "st_hw_probe.h"

#include <mint/cookie.h>
#include <mint/osbind.h>

int ST_Hw_Machine_Major(void)
{
	long mch = 0;

	if (Getcookie(C__MCH, &mch) != C_FOUND) {
		return -1;
	}
	return (int)((unsigned long)mch >> 16);
}

int ST_Hw_Is_Ste_Class(void)
{
	return ST_Hw_Machine_Major() == 1 ? 1 : 0;
}

int ST_Hw_Is_Falcon_Class(void)
{
	return ST_Hw_Machine_Major() == 3 ? 1 : 0;
}

int ST_Hw_Is_Ste_Sound_Class(void)
{
	int const hw = ST_Hw_Machine_Major();

	return (hw == 1 || hw == 2) ? 1 : 0;
}

int ST_Hw_Dma_Audio_Available(void)
{
	int const hw = ST_Hw_Machine_Major();
	long snd = 0;

	if (hw < 0) {
		return 0;
	}
	/* Plain ST / Mega ST — no DMA digitized audio. */
	if (hw == 0) {
		return 0;
	}
	if (Getcookie(C__SND, &snd) == C_FOUND) {
		return (snd & 2L) != 0L ? 1 : 0;
	}
	/* Pre-_SND TOS on STE only. */
	return hw == 1 ? 1 : 0;
}

/* phystop: top of ST-RAM. ST-RAM is contiguous from 0, so this is the whole
   test. Read under Supexec because callers need not be supervisor. */
static long ST_Hw_Read_Phystop(void)
{
	return *(const volatile long *)0x42EL;
}

int ST_Hw_Is_St_Ram(const void *addr)
{
	static unsigned long phystop = 0uL;

	if (phystop == 0uL) {
		phystop = (unsigned long)Supexec(ST_Hw_Read_Phystop);
	}
	return (unsigned long)addr < phystop ? 1 : 0;
}
