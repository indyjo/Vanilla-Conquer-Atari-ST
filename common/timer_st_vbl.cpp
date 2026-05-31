/*
 * timer_st_vbl.cpp — TOS _frclock ($466) for WinTimerClass on Atari ST.
 */

#ifdef ATARI_ST

#include "timer_st_vbl.h"

#include "timer.h"

#include <mint/sysvars.h>

static unsigned long g_st_vbl_origin;
static unsigned char g_st_vbl_ready;

void St_Vbl_Timer_Init(void)
{
	g_st_vbl_origin = *_frclock;
	g_st_vbl_ready = 1;
	WinTickCount.Start();
}

unsigned long St_Vbl_Timer_Now(void)
{
	if (!g_st_vbl_ready) {
		return 0;
	}
	return *_frclock - g_st_vbl_origin;
}

#endif /* ATARI_ST */
