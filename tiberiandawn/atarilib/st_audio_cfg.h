/*
 * st_audio_cfg.h - CONQUER.INI [AtariST] Audio= / StvqEnableAudio= and digi backend flags.
 */
#ifndef ST_AUDIO_CFG_H
#define ST_AUDIO_CFG_H

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

typedef enum StAudioDriver {
	ST_AUDIO_AUTO = 0,
	ST_AUDIO_STE,
	ST_AUDIO_YM,
	ST_AUDIO_COVOX,
	ST_AUDIO_NONE
} StAudioDriver;

/* Parsed from INI before Audio_Init; default Auto / StvqEnableAudio=1. */
extern StAudioDriver g_st_audio_driver_preference;
extern int g_st_stvq_enable_audio;

/* Resolved at Audio_Init: which backend actually runs. */
extern StAudioDriver g_st_audio_backend;

/* 1 when YM/Covox game mix should early-÷2 on stream formats (never for STE). */
extern int g_st_audio_subsample_2;

void ST_Audio_Cfg_Set_From_Ini(char const* audio_str, int stvq_enable_audio);
char const* ST_Audio_Cfg_Driver_Name(StAudioDriver d);
int ST_Audio_Cfg_Is_Timer_Backend(void);
int ST_Audio_Cfg_Digi_Ok(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* ST_AUDIO_CFG_H */
