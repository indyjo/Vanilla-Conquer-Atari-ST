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

// Portable implementation of Random() - matches WIN32LIB behavior
// This implements the same LFSR (Linear Feedback Shift Register) algorithm
// as the x86 assembly version in WIN32LIB/irandom.cpp
extern "C" unsigned long RandNumb = 0x12349876;

unsigned char Random(void)
{
	unsigned char *bytes = (unsigned char *)&RandNumb;
	unsigned char al = bytes[0];
	unsigned char carry;
	
	// shr al,1 (shift right 1 bit, bit0 in carry)
	carry = al & 1;
	al >>= 1;
	
	// shr al,1 (shift right 1 bit again)
	unsigned char carry2 = al & 1;
	al >>= 1;
	
	// rcl [BYTE PTR esi+2],1 (rotate left through carry on byte 3)
	unsigned char old_byte3 = bytes[2];
	bytes[2] = (bytes[2] << 1) | carry2;
	carry2 = (old_byte3 >> 7) & 1;
	
	// rcl [BYTE PTR esi+1],1 (rotate left through carry on byte 2)
	unsigned char old_byte2 = bytes[1];
	bytes[1] = (bytes[1] << 1) | carry2;
	carry2 = (old_byte2 >> 7) & 1;
	
	// cmc (complement carry)
	carry = carry ? 0 : 1;
	
	// sbb al,[esi] (subtract with borrow: al = al - bytes[0] - (1 - carry))
	unsigned char old_al = al;
	al = al - bytes[0] - (1 - carry);
	carry = (old_al < bytes[0] || (old_al == bytes[0] && carry == 0)) ? 1 : 0;
	
	// shr al,1 (shift right 1 bit, sets carry)
	carry2 = al & 1;
	al >>= 1;
	
	// rcr [BYTE PTR esi],1 (rotate right through carry on byte 0)
	unsigned char old_byte0 = bytes[0];
	bytes[0] = (bytes[0] >> 1) | (carry2 << 7);
	
	// mov al,[esi] (reload byte 0)
	al = bytes[0];
	
	// xor al,[esi+1] (xor with byte 1)
	al ^= bytes[1];
	
	return al;
}

