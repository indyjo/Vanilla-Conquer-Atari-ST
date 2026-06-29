/*
 * iconset.cpp - Icon set functions for Atari ST/MiNT
 *
 * This provides implementations compatible with WIN32LIB/iconset.cpp
 */

#include "tile.h"
#include "memflag.h"  /* For Add_Long_To_Pointer */
#include "st16_iconset.h"

static long ReadLE32_Icon(const unsigned char *p)
{
	return (long)((unsigned long)p[0]
		| ((unsigned long)p[1] << 8)
		| ((unsigned long)p[2] << 16)
		| ((unsigned long)p[3] << 24));
}

void * Get_Icon_Set_Map(void const *iconset)
{
	const IControl_Type *ic;
	long map_offset;

	if (!iconset) {
		return NULL;
	}

	ic = (const IControl_Type *)iconset;
	if (ST16_Has_Native_Chunk(ic)) {
		map_offset = (long)ic->Map;
	} else {
		map_offset = ReadLE32_Icon((const unsigned char *)iconset + 28);
	}
	if (map_offset <= 0) {
		return NULL;
	}
	return Add_Long_To_Pointer(iconset, map_offset);
}
