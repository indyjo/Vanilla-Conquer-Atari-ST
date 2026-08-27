/*
 * digi_audio.h - Generic low-level digi playback HAL (function pointers).
 *
 * Clients always pass 8-bit signed PCM; rate flags describe the buffer.
 * Digi_Submit converts to device-native ring bytes (YM s&$FC (signed LUT), Covox unsigned).
 */
#ifndef DIGI_AUDIO_H
#define DIGI_AUDIO_H

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

enum {
	DIGI_RATE_6250 = 1 << 0,
	DIGI_RATE_12500 = 1 << 1
};

typedef struct DigiInfo {
	unsigned device_rate_hz; /* ~6250, ~12500, or ~25000 */
	unsigned ring_samples;
} DigiInfo;

typedef DigiInfo const* (*Digi_Info_Fn)(void);
typedef void const* (*Digi_Submit_Fn)(void const* start, void const* end, unsigned rate_flags);
typedef unsigned (*Digi_Capacity_Fn)(unsigned rate_flags);
typedef int (*Digi_Active_Fn)(void); /* 1 while the device is consuming the ring */
typedef void (*Digi_Pause_Fn)(void);
typedef void (*Digi_Resume_Fn)(void);
typedef void (*Digi_Flush_Fn)(void);
typedef void (*Digi_Shutdown_Fn)(void);

extern Digi_Info_Fn Digi_Info;
extern Digi_Submit_Fn Digi_Submit;
extern Digi_Capacity_Fn Digi_Capacity;
extern Digi_Active_Fn Digi_Active;
extern Digi_Pause_Fn Digi_Pause;
extern Digi_Resume_Fn Digi_Resume;
extern Digi_Flush_Fn Digi_Flush;
extern Digi_Shutdown_Fn Digi_Shutdown;

/* Clear all Digi_* pointers (also done by Digi_Shutdown implementations). */
void Digi_Clear_Hooks(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* DIGI_AUDIO_H */
