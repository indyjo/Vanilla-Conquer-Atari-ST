/*
 * wwstd.h - Westwood standard definitions for Atari ST/MiNT
 * 
 * Provides color constants and other standard definitions used by main source code.
 */

#ifndef WWSTD_H
#define WWSTD_H

#include "windows.h"

// Macros used in function.h
#define LOW_WORD(a) ((unsigned short)((long)(a) & 0x0000FFFFL))
#define HIGH_WORD(a) ((unsigned long)(a) >> 16)
#define MAKE_LONG(a,b) (((long)(a) << 16) | (long)((b)&0x0000FFFFL))

// Color constants - used in defines.h
typedef enum {
	TBLACK,
	PURPLE,
	CYAN,
	GREEN,
	LTGREEN,
	YELLOW,
	PINK,
	BROWN,
	RED,
	LTCYAN,
	LTBLUE,
	BLUE,
	BLACK,
	GREY,
	LTGREY,
	WHITE,
	COLOR_PADDING=0x1000
} ColorType;

// Make color constants available as macros too
#define TBLACK   0
#define PURPLE   1
#define CYAN     2
#define GREEN    3
#define LTGREEN  4
#define YELLOW   5
#define PINK     6
#define BROWN    7
#define RED      8
#define LTCYAN   9
#define LTBLUE  10
#define BLUE    11
#define BLACK   12
#define GREY    13
#define LTGREY  14
#define WHITE   15

#endif /* WWSTD_H */

