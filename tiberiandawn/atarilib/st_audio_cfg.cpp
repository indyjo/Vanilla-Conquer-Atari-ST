/*
 * st_audio_cfg.cpp - Parse and hold digi driver preference from CONQUER.INI.
 */
#ifdef ATARI_ST

#include "st_audio_cfg.h"

#include <ctype.h>
#include <string.h>

StAudioDriver g_st_audio_driver_preference = ST_AUDIO_AUTO;
int g_st_stvq_enable_audio = 1;
StAudioDriver g_st_audio_backend = ST_AUDIO_NONE;
int g_st_audio_subsample_2 = 0;

static int st_audio_ieq(char const* a, char const* b)
{
	if (!a || !b) {
		return 0;
	}
	while (*a && *b) {
		int ca = (unsigned char)*a++;
		int cb = (unsigned char)*b++;
		if (tolower(ca) != tolower(cb)) {
			return 0;
		}
	}
	return *a == 0 && *b == 0;
}

void ST_Audio_Cfg_Set_From_Ini(char const* audio_str, int stvq_enable_audio)
{
	g_st_stvq_enable_audio = stvq_enable_audio != 0 ? 1 : 0;
	if (!audio_str || audio_str[0] == '\0') {
		g_st_audio_driver_preference = ST_AUDIO_AUTO;
		return;
	}
	while (*audio_str == ' ' || *audio_str == '\t') {
		++audio_str;
	}
	if (st_audio_ieq(audio_str, "STE") || st_audio_ieq(audio_str, "STe") || st_audio_ieq(audio_str, "DMA")) {
		g_st_audio_driver_preference = ST_AUDIO_STE;
	} else if (st_audio_ieq(audio_str, "YM") || st_audio_ieq(audio_str, "YM2149") || st_audio_ieq(audio_str, "PSG")) {
		g_st_audio_driver_preference = ST_AUDIO_YM;
	} else if (st_audio_ieq(audio_str, "Covox") || st_audio_ieq(audio_str, "COVOX")) {
		g_st_audio_driver_preference = ST_AUDIO_COVOX;
	} else if (st_audio_ieq(audio_str, "None") || st_audio_ieq(audio_str, "Off") || st_audio_ieq(audio_str, "0")) {
		g_st_audio_driver_preference = ST_AUDIO_NONE;
	} else {
		g_st_audio_driver_preference = ST_AUDIO_AUTO;
	}
}

char const* ST_Audio_Cfg_Driver_Name(StAudioDriver d)
{
	switch (d) {
	case ST_AUDIO_STE:
		return "STE";
	case ST_AUDIO_YM:
		return "YM";
	case ST_AUDIO_COVOX:
		return "Covox";
	case ST_AUDIO_NONE:
		return "None";
	case ST_AUDIO_AUTO:
	default:
		return "Auto";
	}
}

int ST_Audio_Cfg_Is_Timer_Backend(void)
{
	return g_st_audio_backend == ST_AUDIO_YM || g_st_audio_backend == ST_AUDIO_COVOX;
}

int ST_Audio_Cfg_Digi_Ok(void)
{
	return g_st_audio_backend != ST_AUDIO_NONE;
}

#endif /* ATARI_ST */
