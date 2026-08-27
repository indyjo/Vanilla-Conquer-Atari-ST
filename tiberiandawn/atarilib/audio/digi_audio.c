/*
 * digi_audio.c - Digi_* HAL function-pointer globals.
 */
#ifdef ATARI_ST

#include "audio/digi_audio.h"

Digi_Info_Fn Digi_Info;
Digi_Submit_Fn Digi_Submit;
Digi_Capacity_Fn Digi_Capacity;
Digi_Active_Fn Digi_Active;
Digi_Pause_Fn Digi_Pause;
Digi_Resume_Fn Digi_Resume;
Digi_Flush_Fn Digi_Flush;
Digi_Shutdown_Fn Digi_Shutdown;

void Digi_Clear_Hooks(void)
{
	Digi_Info = 0;
	Digi_Submit = 0;
	Digi_Capacity = 0;
	Digi_Active = 0;
	Digi_Pause = 0;
	Digi_Resume = 0;
	Digi_Flush = 0;
	Digi_Shutdown = 0;
}

#endif /* ATARI_ST */
