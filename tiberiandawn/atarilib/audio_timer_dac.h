/*
 * audio_timer_dac.h - MFP Timer A digi output (YM-2149 movep / Covox Port B).
 */
#ifndef AUDIO_TIMER_DAC_H
#define AUDIO_TIMER_DAC_H

#ifdef ATARI_ST

#include "digi_ring.h"
#include "st_audio_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { TIMER_DAC_RING_BYTES = DIGI_RING_BYTES };

int Timer_Dac_Init(StAudioDriver sink); /* ST_AUDIO_YM or ST_AUDIO_COVOX */
void Timer_Dac_Shutdown(void);

DigiRing* Timer_Dac_Ring(void);
DigiRingOps const* Timer_Dac_Ops(void);

/* Game mix: stride 1; STVQ: stride 2. Call before arming movie fill. */
void Timer_Dac_Set_Stride(unsigned stride);
void Timer_Dac_Set_Movie_Owner(int movie_owns); /* 1 = STVQ filling ring, pause game mix */

/* Silence / pause timer without freeing ring (StvqEnableAudio=0). */
void Timer_Dac_Pause(void);
void Timer_Dac_Resume(void);

/* After game mixer prefilled [0 .. prefill), arm timer with stride 1. */
void Timer_Dac_Game_Arm(unsigned prefill);
void Timer_Dac_Game_Stop(void);

int Timer_Dac_Active(void);
int Timer_Dac_Movie_Owns(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* AUDIO_TIMER_DAC_H */
