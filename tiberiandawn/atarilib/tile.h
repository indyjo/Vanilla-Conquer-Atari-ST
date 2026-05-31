/*
 * tile.h - Tile header for Atari ST/MiNT
 */

#ifndef TILE_H
#define TILE_H

#include <stdint.h>

/*=========================================================================
 * The following prototypes are for the file: ICONSET.CPP
 *=========================================================================*/
void* Load_Icon_Set(char const* filename, void* iconsetptr, long buffsize);
void Free_Icon_Set(void const* iconset);
long Get_Icon_Set_Size(void const* iconset);
int Get_Icon_Set_Width(void const* iconset);
int Get_Icon_Set_Height(void const* iconset);
void* Get_Icon_Set_Icondata(void const* iconset);
void* Get_Icon_Set_Trans(void const* iconset);
void* Get_Icon_Set_Remapdata(void const* iconset);
void* Get_Icon_Set_Palettedata(void const* iconset);
int Get_Icon_Set_Count(void const* iconset);
void* Get_Icon_Set_Map(void const* iconset);

/*
** This is the control structure at the start of a loaded icon set.  It must match
** the structure in WWLIB.I!  This structure MUST be a multiple of 16 bytes long.
*/

// C&C version of struct
#pragma pack(push, 2)
typedef struct
{
    int16_t Width;     // Width of icons (pixels).
    int16_t Height;    // Height of icons (pixels).
    int16_t Count;     // Number of (logical) icons in this set.
    int16_t Allocated; // Was this iconset allocated?
    int32_t Size;      // Size of entire iconset memory block.
    int32_t Icons;     // Offset from buffer start to icon data.
    int32_t Palettes;  // Offset from buffer start to palette data.
    int32_t Remaps;    // Offset from buffer start to remap index data.
    int32_t TransFlag; // Offset for transparency flag table.
    int32_t Map;       // Icon map offset (if present).
} IControl_Type;
#pragma pack(pop)

#endif /* TILE_H */
