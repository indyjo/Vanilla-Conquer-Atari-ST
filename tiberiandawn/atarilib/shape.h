/*
 * shape.h - Shape header stub for Atari ST/MiNT
 */

#ifndef SHAPE_H
#define SHAPE_H

// ShapeFlags_Type used in function.h
typedef enum {
	SHAPE_NORMAL 			= 0x0000,		// Standard shape
	SHAPE_HORZ_REV 		= 0x0001,		// Flipped horizontally
	SHAPE_VERT_REV 		= 0x0002,		// Flipped vertically
	SHAPE_SCALING 			= 0x0004,		// Scaled (WORD scale_x, WORD scale_y)
	SHAPE_VIEWPORT_REL 	= 0x0010,		// Coords are window-relative
	SHAPE_WIN_REL 			= 0x0010,		// Coordinates are window relative instead of absolute.
	SHAPE_CENTER 			= 0x0020,		// Coords are based on shape's center pt
	SHAPE_BOTTOM			= 0x0040,		// Y coord is based on shape's bottom pt
	SHAPE_FADING 			= 0x0100,		// Fading effect
	SHAPE_PREDATOR 		= 0x0200,		// Transparent warping effect
	SHAPE_COMPACT 			= 0x0400,		// Never use this bit
	SHAPE_PRIORITY 		= 0x0800,		// Use priority system when drawing
	SHAPE_GHOST				= 0x1000,		// Shape is drawn ghosted
	SHAPE_SHADOW			= 0x2000,
	SHAPE_PARTIAL  		= 0x4000,
	SHAPE_COLOR 			= 0x8000			// Remap the shape's colors
} ShapeFlags_Type;

inline ShapeFlags_Type operator|(ShapeFlags_Type a, ShapeFlags_Type b)
{return static_cast<ShapeFlags_Type>(static_cast<int>(a) | static_cast<int>(b));}

inline ShapeFlags_Type operator&(ShapeFlags_Type a, ShapeFlags_Type b)
{return static_cast<ShapeFlags_Type>(static_cast<int>(a) & static_cast<int>(b));}

inline ShapeFlags_Type operator~(ShapeFlags_Type a)
{return static_cast<ShapeFlags_Type>(~static_cast<int>(a));}

#endif /* SHAPE_H */

