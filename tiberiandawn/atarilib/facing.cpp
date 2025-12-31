/*
 * facing.cpp - Facing calculation functions for Atari ST/MiNT
 * 
 * Simplified implementations of Desired_Facing256 and Desired_Facing8
 */

#include "facing.h"
#include <math.h>

/*
 * Desired_Facing256 - Calculate facing direction (0-255 resolution)
 * 
 * Returns the facing angle from source to destination.
 * North is 0, East is 64, South is 128, West is 192.
 */
int Desired_Facing256(long srcx, long srcy, long dstx, long dsty)
{
	long dx = dstx - srcx;
	long dy = dsty - srcy;
	
	if (dx == 0 && dy == 0) {
		return 0; /* Same position */
	}
	
	/* Calculate angle using atan2, then convert to 0-255 range */
	double angle = atan2((double)dy, (double)dx);
	
	/* Convert from radians to 0-255 range */
	/* atan2 returns -PI to PI, we want 0 to 2*PI */
	if (angle < 0) {
		angle += 2.0 * 3.14159265358979323846;
	}
	
	/* Convert to 0-255 range (256 directions) */
	int facing = (int)((angle * 256.0) / (2.0 * 3.14159265358979323846));
	
	/* Adjust so North (up, negative Y) is 0 */
	facing = (facing + 64) % 256;
	
	return facing;
}

/*
 * Desired_Facing8 - Calculate facing direction (8 directions)
 * 
 * Returns the facing angle as a multiple of 32 (0, 32, 64, 96, 128, 160, 192, 224)
 */
int Desired_Facing8(long x1, long y1, long x2, long y2)
{
	long dx = x2 - x1;
	long dy = y2 - y1;
	
	if (dx == 0 && dy == 0) {
		return -1; /* Same position */
	}
	
	/* Calculate 256-direction facing first */
	int facing256 = Desired_Facing256(x1, y1, x2, y2);
	
	/* Convert to 8 directions (0, 32, 64, 96, 128, 160, 192, 224) */
	/* Round to nearest multiple of 32 */
	int facing8 = ((facing256 + 16) / 32) * 32;
	
	return facing8 % 256;
}

