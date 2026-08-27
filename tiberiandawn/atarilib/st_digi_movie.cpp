/*
 * st_digi_movie.cpp - Game vs STVQ ownership of the Digi HAL (above atarilib/audio/).
 */
#ifdef ATARI_ST

#include "st_digi_movie.h"

static volatile int g_movie_owns;

void Digi_Movie_Set_Owns(int owns)
{
	g_movie_owns = owns ? 1 : 0;
}

int Digi_Movie_Owns(void)
{
	return g_movie_owns;
}

#endif /* ATARI_ST */
