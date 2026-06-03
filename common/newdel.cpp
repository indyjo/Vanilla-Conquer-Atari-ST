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

/***************************************************************************
 **     C O N F I D E N T I A L --- W E S T W O O D   S T U D I O S       **
 ***************************************************************************
 *                                                                         *
 *                 Project Name : Memory system.                           *
 *                                                                         *
 *                    File Name : NEWDEL.CPP                               *
 *                                                                         *
 *                   Programmer : Scott K. Bowen                           *
 *                                                                         *
 *                   Start Date : June 21, 1994                            *
 *                                                                         *
 *                  Last Update : October 20, 1994   [SKB]                 *
 *                                                                         *
 *-------------------------------------------------------------------------*
 * Functions:                                                              *
 *   operator NEW -- Overides the global new function.                     *
 *   operator delete -- Overides the global delete function.               *
 *   operator NEW[] -- Overides the array version of new.                  *
 *   operator delete[] -- Overides the array version of delete[]           *
 *   operator NEW (std::nothrow) -- Atari ST only; see NEWDEL.CPP          *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "wwmem.h"

#ifdef ATARI_ST
#include <new>
#endif

/*=========================================================================*/
/* The following PRIVATE functions are in this file:                       */
/*=========================================================================*/

/*= = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =*/

/***************************************************************************
 * OPERATOR NEW -- Overides the global new function.                       *
 *                                                                         *
 * INPUT:                                                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   06/21/1994 SKB : Created.                                             *
 *=========================================================================*/
void* operator new(size_t size)
{
    return (Alloc((unsigned int)size, MEM_NEW));
}

/***************************************************************************
 * OPERATOR NEW[] -- Overides the array version of new.                    *
 *                                                                         *
 *                                                                         *
 *                                                                         *
 * INPUT:                                                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   06/21/1994 SKB : Created.                                             *
 *=========================================================================*/
void* operator new[](size_t size)
{
    return (Alloc((unsigned int)size, MEM_NEW));
}

#ifdef ATARI_ST
/***************************************************************************
 * OPERATOR NEW (std::nothrow) -- Atari ST; same as operator new above.   *
 *                                                                         *
 * Required when the game is built with -mfastcall: new (std::nothrow)    *
 * otherwise uses libsupc++'s operator new(..., nothrow_t), which breaks  *
 * in practice (__Unwind_SjLj_Register overwrites the jsr return address, *
 * then rts to 0). Provide this thin Alloc wrapper so the call stays in   *
 * game code compiled with the same -mfastcall flags as the rest of C&C.  *
 *=========================================================================*/
void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    return (Alloc((unsigned int)size, MEM_NEW));
}

/***************************************************************************
 * OPERATOR NEW[] (std::nothrow) -- Atari ST array form.                   *
 *=========================================================================*/
void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return (Alloc((unsigned int)size, MEM_NEW));
}
#endif /* ATARI_ST */

/***************************************************************************
 * OPERATOR DELETE -- Overides the global delete function.                 *
 *                                                                         *
 *                                                                         *
 *                                                                         *
 * INPUT:                                                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   06/21/1994 SKB : Created.                                             *
 *=========================================================================*/
void operator delete(void* ptr)
{
    Free(ptr);
}

/***************************************************************************
 * OPERATOR DELETE[] -- Overides the array version of delete[]           	*
 *                                                                         *
 *                                                                         *
 *                                                                         *
 * INPUT:                                                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   10/20/1994 SKB : Created.                                             *
 *=========================================================================*/
void operator delete[](void* ptr)
{
    Free(ptr);
}
