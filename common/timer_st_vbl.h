/*
 * timer_st_vbl.h — Atari ST vertical-blank tick source for WinTimerClass / TimerClass.
 *
 * Reads TOS _frclock ($466), incremented by the OS on every VBL interrupt.
 * Must call St_Vbl_Timer_Init() once after Super(0): low memory sysvars bus-error in user mode.
 */

#ifndef COMMON_TIMER_ST_VBL_H_
#define COMMON_TIMER_ST_VBL_H_

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

/* Call from main after Super(0); captures baseline, enables _frclock reads, starts WinTickCount. */
void St_Vbl_Timer_Init(void);

/* Elapsed VBL ticks since St_Vbl_Timer_Init(); returns 0 before init. */
unsigned long St_Vbl_Timer_Now(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* COMMON_TIMER_ST_VBL_H_ */
