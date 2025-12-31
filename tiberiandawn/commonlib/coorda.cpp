//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free 
// software: you can redistribute it and/or modify it under the terms of 
// the GNU General Public License as published by the Free Software Foundation, 
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed 
// in the hope that it will be useful, but with permitted additional restrictions 
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT 
// distributed with this program. You should have received a copy of the 
// GNU General Public License along with permitted additional restrictions 
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/*
 * coorda.cpp - Coordinate conversion functions
 * 
 * C/C++ implementations of fixed-point coordinate conversion functions.
 * Originally implemented in x86 assembly (coorda.asm).
 */

#include "coorda.h"

/*
 * Cardinal_To_Fixed - Convert cardinal number to fixed-point representation
 * 
 * Returns: (cardinal * 256) / base, or 65535 if base == 0
 */
unsigned Cardinal_To_Fixed(unsigned base, unsigned cardinal)
{
	if (base == 0) {
		return 0xFFFF; // Return 65535 if base is 0
	}
	
	// Return (cardinal * 256) / base
	return (cardinal << 8) / base;
}

/*
 * Fixed_To_Cardinal - Convert fixed-point number to cardinal representation
 * 
 * Returns: (fixed * base) / 256
 */
unsigned Fixed_To_Cardinal(unsigned base, unsigned fixed)
{
	// Return (fixed * base) / 256
	return (fixed * base) >> 8;
}

