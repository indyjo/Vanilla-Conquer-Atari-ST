/*
 * tile.h - Tile header for Atari ST/MiNT
 */

#ifndef TILE_H
#define TILE_H

/*=========================================================================
 * The following prototypes are for the file: ICONSET.CPP
 *=========================================================================*/
void * Load_Icon_Set(char const *filename, void *iconsetptr, long buffsize);
void Free_Icon_Set(void const *iconset);
long Get_Icon_Set_Size(void const *iconset);
int Get_Icon_Set_Width(void const *iconset);
int Get_Icon_Set_Height(void const *iconset);
void * Get_Icon_Set_Icondata(void const *iconset);
void * Get_Icon_Set_Trans(void const *iconset);
void * Get_Icon_Set_Remapdata(void const *iconset);
void * Get_Icon_Set_Palettedata(void const *iconset);
int Get_Icon_Set_Count(void const *iconset);
void * Get_Icon_Set_Map(void const *iconset);

/*
** This is the control structure at the start of a loaded icon set.  It must match
** the structure in WWLIB.I!  This structure MUST be a multiple of 16 bytes long.
*/

// C&C version of struct
typedef struct {
	short	Width;			// Width of icons (pixels).
	short	Height;			// Height of icons (pixels).
	short	Count;			// Number of (logical) icons in this set.
	short	Allocated;		// Was this iconset allocated?
	long	Size;				// Size of entire iconset memory block.
	unsigned char *Icons;			// Offset from buffer start to icon data.
	long	Palettes;		// Offset from buffer start to palette data.
	long	Remaps;			// Offset from buffer start to remap index data.
	long	TransFlag;		// Offset for transparency flag table.
	unsigned char *Map;				// Icon map offset (if present).
} IControl_Type;

#endif /* TILE_H */

