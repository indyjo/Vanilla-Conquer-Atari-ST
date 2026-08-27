/*
 * st_digi_movie.h - Game vs STVQ producer ownership of the Digi HAL.
 *
 * Above the atarilib/audio hardware layer. Yield/reclaim of mixer VBL lives in audio.cpp.
 */
#ifndef ST_DIGI_MOVIE_H
#define ST_DIGI_MOVIE_H

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

void Digi_Movie_Set_Owns(int owns);
int Digi_Movie_Owns(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* ST_DIGI_MOVIE_H */
