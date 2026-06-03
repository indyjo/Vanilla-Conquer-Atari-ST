#ifndef ST_AUDIO_MIX_TEST_H
#define ST_AUDIO_MIX_TEST_H

/*
 * Interactive dual-voice STE DMA mix test (same VBL servicing as in-game ATARI_ST).
 * Pick two .AUD clips from MIX archives, set per-voice volume, play together.
 * Call from the audio submenu (supervisor); do not nest Super/SuperToUser inside.
 */
int st_run_interactive_audio_dual_mix(void);

#endif
