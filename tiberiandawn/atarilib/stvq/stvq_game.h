/*
 * stvq_game.h - Tiberian Dawn glue for FORM STVQ (CCFile, game screens, mixer yield).
 *
 * Shared decode/hw stays in stvq_player.c / stvq_hw.c (also used by stvqview).
 */
#ifndef STVQ_GAME_H
#define STVQ_GAME_H

#ifdef ATARI_ST

#ifdef __cplusplus

/*
 * Open mix path `fullname` (.VQA name) and play if it is FORM STVQ.
 * Missing / short / non-STVQ clips soft-skip (return 0).
 * Returns 1 if ESC aborted playback (caller should clear the screen).
 */
int Stvq_Play_Named_Movie(char const* fullname, int use_audio);

#endif /* __cplusplus */

#endif /* ATARI_ST */

#endif /* STVQ_GAME_H */
