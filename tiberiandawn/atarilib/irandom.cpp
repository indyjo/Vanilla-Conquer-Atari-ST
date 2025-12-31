/*
 * irandom.cpp - Random number generation for Atari ST/MiNT
 */

#include "misc.h"
#include <stdlib.h>
#include <time.h>

static bool initialized = false;

int IRandom(int minval, int maxval)
{
	if (!initialized) {
		srand((unsigned int)time(NULL));
		initialized = true;
	}
	
	if (minval > maxval) {
		int temp = minval;
		minval = maxval;
		maxval = temp;
	}
	
	if (minval == maxval) {
		return minval;
	}
	
	return minval + (rand() % (maxval - minval + 1));
}

