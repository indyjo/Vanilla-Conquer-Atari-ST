/*
 * audio_dma.h - STE-era 8-bit DMA digi output (STE / TT / Falcon).
 * Installs Digi_* HAL hooks on init; teardown via Digi_Shutdown.
 */
#ifndef AUDIO_DMA_H
#define AUDIO_DMA_H

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

/* Probe DMA, allocate the ST-RAM ring, install Digi_* hooks. 0 on failure. */
int Audio_Dma_Init(void);

/* Prefer Digi_Shutdown(); this remains for Audio_Init teardown ordering. */
void Audio_Dma_Shutdown(void);

int Audio_Dma_Inited(void);

/* TOS $FF8921 / Falcon codec clock, captured before first change and restored at Sound_End. */
void Audio_Dma_Save_Tos_Sound(void);
void Audio_Dma_Restore_Tos_Sound(void);

/* Stop DMA and reconnect STE mixer / Falcon matrix (movie reclaim). */
void Audio_Dma_Connect_Output(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* AUDIO_DMA_H */
