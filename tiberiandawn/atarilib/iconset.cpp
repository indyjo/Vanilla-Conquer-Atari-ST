/*
 * iconset.cpp - Icon set functions for Atari ST/MiNT
 * 
 * This provides implementations compatible with WIN32LIB/iconset.cpp
 */

#include "tile.h"
#include "../COMMONLIB/wwstd.h"
#include "memflag.h"  // For Add_Long_To_Pointer

/***************************************************************************
 * Get_Icon_Set_Map -- Gets the map pointer from an icon set              *
 *                                                                         *
 * INPUT:		void const *iconset - pointer to icon set                  *
 *                                                                         *
 * OUTPUT:     void* - pointer to map data, or NULL                         *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/iconset.cpp                                     *
 *=========================================================================*/
void * Get_Icon_Set_Map(void const *iconset)
{
	IControl_Type	*icontrol;
	icontrol = (IControl_Type *)iconset;
	if (icontrol)
		return(Add_Long_To_Pointer(iconset, (long)icontrol->Map));
	return(NULL);
}

