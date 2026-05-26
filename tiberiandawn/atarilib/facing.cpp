/*
 * facing.cpp - Facing calculation functions for Atari ST/MiNT
 *
 * Portable C implementation of the original Westwood facing logic.
 */

#include "../facing.h"  // For FacingClass if needed, but mainly for consistency

// Function declarations (matching wwlib32.h)
#ifdef __cplusplus
extern "C" {
#endif
int Desired_Facing256(long srcx, long srcy, long dstx, long dsty);
int Desired_Facing8(long x1, long y1, long x2, long y2);
#ifdef __cplusplus
}
#endif

static const unsigned char NewFacing8[16] = {
	1, 2, 1, 0,
	7, 6, 7, 0,
	3, 2, 3, 4,
	5, 6, 5, 4
};

/*
 * Desired_Facing256 - Calculate facing direction (0-255 resolution)
 * 
 * Returns the facing angle from source to destination.
 * North is 0, East is 64, South is 128, West is 192.
 */
int Desired_Facing256(long srcx, long srcy, long dstx, long dsty)
{
	unsigned long facing = 0;
	unsigned long abs_x;
	unsigned long abs_y;
	unsigned long axis_adjust;

	/*
	 * Mirror the original Win32 assembly algorithm: derive the quadrant from
	 * the signs, then compute a pseudo-angle from the minor/major axis ratio.
	 */
	long xdiff = dstx - srcx;
	if (xdiff < 0) {
		abs_x = (unsigned long)(-xdiff);
		facing = 0xC0;
	} else {
		abs_x = (unsigned long)xdiff;
	}

	long ydiff = srcy - dsty;
	if (ydiff < 0) {
		abs_y = (unsigned long)(-ydiff);
		facing ^= 0x40;
	} else {
		abs_y = (unsigned long)ydiff;
	}

	axis_adjust = (facing & 0x40) ^ 0x40;

	if (abs_y >= abs_x) {
		unsigned long temp = abs_x;
		abs_x = abs_y;
		abs_y = temp;
		axis_adjust ^= 0x40;
	}

	if ((abs_y & 0xFFFFFF00UL) == 0) {
		while ((abs_x & 0xFFFFFF00UL) != 0) {
			abs_x >>= 1;
			abs_y >>= 1;
		}
	}

	if (abs_x == 0) {
		abs_y = 0xFFFFFFFFUL;
	} else {
		abs_y = (unsigned long)(((unsigned long long)abs_y << 8) / abs_x);
	}

	abs_y >>= 3;
	if (axis_adjust != 0) {
		axis_adjust--;
		abs_y = (unsigned long)(-(long)abs_y);
	}

	return (int)((abs_y + axis_adjust + facing) & 0xFF);
}

/*
 * Desired_Facing8 - Calculate facing direction (8 directions)
 * 
 * Returns the facing angle as a multiple of 32 (0, 32, 64, 96, 128, 160, 192, 224)
 */
int Desired_Facing8(long x1, long y1, long x2, long y2)
{
	unsigned int index = 0;
	unsigned long abs_x;
	unsigned long abs_y;
	unsigned long greater;
	unsigned long lesser;

	if (x1 == x2 && y1 == y2) {
		return -1; /* Same position */
	}

	long ydiff = y1 - y2;
	if (ydiff < 0) {
		index |= 0x8;
		abs_y = (unsigned long)(-ydiff);
	} else {
		abs_y = (unsigned long)ydiff;
	}

	long xdiff = x2 - x1;
	if (xdiff < 0) {
		index |= 0x4;
		abs_x = (unsigned long)(-xdiff);
	} else {
		abs_x = (unsigned long)xdiff;
	}

	if (abs_x < abs_y) {
		index |= 0x2;
		greater = abs_y;
		lesser = abs_x;
	} else {
		greater = abs_x;
		lesser = abs_y;
	}

	if (lesser < ((greater + 1) >> 1)) {
		index |= 0x1;
	}

	return (int)(NewFacing8[index] << 5);
}

