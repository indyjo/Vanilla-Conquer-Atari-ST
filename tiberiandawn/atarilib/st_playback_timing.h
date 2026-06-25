#ifndef ST_PLAYBACK_TIMING_H
#define ST_PLAYBACK_TIMING_H

#ifdef ATARI_ST
void StPlaybackTiming_Start(void);
void StPlaybackTiming_EndAndPrint(void);
#else
inline void StPlaybackTiming_Start(void)
{
}
inline void StPlaybackTiming_EndAndPrint(void)
{
}
#endif

#endif /* ST_PLAYBACK_TIMING_H */
