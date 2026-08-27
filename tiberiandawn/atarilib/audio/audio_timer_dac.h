/*
 * audio_timer_dac.h - MFP Timer A digi output (YM-2149 movep / Covox Port B).
 * Installs Digi_* HAL hooks on init; teardown via Digi_Shutdown.
 */
#ifndef AUDIO_TIMER_DAC_H
#define AUDIO_TIMER_DAC_H

#ifdef ATARI_ST

#include "st_audio_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Driver-specific init; installs Digi_* function pointers. */
int Timer_Dac_Init(StAudioDriver sink); /* ST_AUDIO_YM or ST_AUDIO_COVOX */

/* Prefer Digi_Shutdown(); this remains for Audio_Init teardown ordering. */
void Timer_Dac_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* AUDIO_TIMER_DAC_H */
