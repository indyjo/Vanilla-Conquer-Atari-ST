#ifndef ST_PLAYBACK_TIMING_H
#define ST_PLAYBACK_TIMING_H

#ifdef ATARI_ST
#ifdef __GNUC__
#define ST_PLAYBACK_TIMING_HOOK __attribute__((noinline, noclone, used))
#else
#define ST_PLAYBACK_TIMING_HOOK
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Hatari breakpoint names: no '()' (debugger treats parens as tokens). */
ST_PLAYBACK_TIMING_HOOK void HatariProfileStart(void);
ST_PLAYBACK_TIMING_HOOK void HatariProfileEnd(void);
#ifdef __cplusplus
}
void StPlaybackTiming_Start(void);
void StPlaybackTiming_EndAndPrint(void);
#endif
#else
inline void StPlaybackTiming_Start(void)
{
}
inline void StPlaybackTiming_EndAndPrint(void)
{
}
#endif

#endif /* ST_PLAYBACK_TIMING_H */
