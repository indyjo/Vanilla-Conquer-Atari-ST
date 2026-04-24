/*
 * iconset.cpp - Icon set functions for Atari ST/MiNT
 * 
 * This provides implementations compatible with WIN32LIB/iconset.cpp
 */

#include "tile.h"
#include "memflag.h"  // For Add_Long_To_Pointer

static short ReadLE16_Icon(const unsigned char *p)
{
	return (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static long ReadLE32_Icon(const unsigned char *p)
{
	return (long)((unsigned long)p[0]
		| ((unsigned long)p[1] << 8)
		| ((unsigned long)p[2] << 16)
		| ((unsigned long)p[3] << 24));
}

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
	if (iconset) {
		const unsigned char *raw = (const unsigned char *)iconset;

		/*
		** Icon set data can be byte-packed in MIX files and may start at odd addresses.
		** On m68k, direct long/short loads from odd addresses trigger Address Error.
		*/
		/*
		** IControl_Type layout (2-byte aligned):
		**   Count @ +4 (short), Size @ +8 (long), Map @ +28 (pointer/long).
		*/
		long map_offset = ReadLE32_Icon(raw + 28);
		long icon_count = (long)ReadLE16_Icon(raw + 4);
		long total_size = ReadLE32_Icon(raw + 8);
		/* Defensive bounds checks: corrupted/missing iconsets can carry invalid offsets. */
		if (map_offset <= 0 || total_size <= 0) {
			return(NULL);
		}
		if (map_offset >= total_size) {
			return(NULL);
		}
		if (icon_count < 0) {
			return(NULL);
		}
		if (map_offset + icon_count > total_size) {
			return(NULL);
		}
		return(Add_Long_To_Pointer(iconset, map_offset));
	}
	return(NULL);
}

