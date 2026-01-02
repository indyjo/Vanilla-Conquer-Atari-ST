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

/*
------------------------------- Shape header --------------------------------
*/
typedef struct {
	unsigned short		ShapeType;			// 0 = normal, 1 = 16 colors,
										//  2 = uncompressed, 4 = 	<16 colors
	unsigned char		Height;				// Height of the shape in scan lines
	unsigned short		Width;				// Width of the shape in bytes
	unsigned char		OriginalHeight;	// Original height of shape in scan lines
	unsigned short		ShapeSize;			// Size of the shape, including header
	unsigned short		DataLength;			// Size of the uncompressed shape (just data)
	unsigned char		Colortable[16];	// Optional color table for compact shape
} Shape_Type;

/*
------------------------------- Shape block ---------------------------------
*/
typedef struct {
	unsigned short		NumShapes;			// number of shapes in the block
	long		Offsets[];			// array of offsets to shape data
										//  (offsets within the shape block, with
										//  0 being the first offset value, not the
										//  start of the shape block)
} ShapeBlock_Type;

/*
******************************** Prototypes *********************************
*/

/*
-------------------------------- prioinit.c ---------------------------------
*/

extern "C" {
extern void  *MaskPage;
extern void  *BackGroundPage;
extern long  _ShapeBufferSize;
extern char  *_ShapeBuffer;
}

class GraphicBufferClass;
void Init_Priority_System (GraphicBufferClass *mask,
											GraphicBufferClass *back);

/*
-------------------------------- drawshp.asm --------------------------------
*/

class GraphicViewPortClass;
extern "C" {
int Draw_Shape(GraphicViewPortClass *gvp, void const *shape, long x, long y, long flags, ...);
}

/*
---------------------------------- shape.c ----------------------------------
*/
short Get_Shape_Data(void const *shape, int data);
int Extract_Shape_Count(void const *buffer);
void * Extract_Shape(void const *buffer, int shape);
int Restore_Shape_Height(void *shape);
int Set_Shape_Height(void const *shape, int newheight);

extern "C" {
int Get_Shape_Width(void const *shape);
int Get_Shape_Height(void const *shape);
int Get_Shape_Original_Height(void const *shape);
int Get_Shape_Uncomp_Size(void const *shape);
}

/*
------------------------------- setshape.asm --------------------------------
*/
extern "C" {
void Set_Shape_Buffer(void const *buffer, int size);
}
/*
------------------------------- shapeinf.asm --------------------------------
*/
int Get_Shape_Flags(void const *shape);
int  Get_Shape_Size(void const *shape);
int  Get_Shape_Scaled_Width(void const *shape, int scale);
int  Get_Shape_Scaled_Height(void const *shape, int scale);

#endif /* SHAPE_H */

