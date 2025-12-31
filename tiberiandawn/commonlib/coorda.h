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
 * coorda.h - Coordinate conversion function declarations
 * 
 * Provides declarations for fixed-point coordinate conversion functions.
 * These were originally in assembly (coorda.asm) for x86, now have C/C++ implementations.
 */

#ifndef COORDA_H
#define COORDA_H

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-point coordinate conversion functions
// These convert between cardinal (integer) and fixed-point representations
unsigned Cardinal_To_Fixed(unsigned base, unsigned cardinal);
unsigned Fixed_To_Cardinal(unsigned base, unsigned fixed);

#ifdef __cplusplus
}
#endif

#endif /* COORDA_H */

