/*
 * filepcx.h - PCX file format definitions for Atari ST/MiNT
 * 
 * This provides a compatible interface to WIN32LIB/filepcx.h
 */

#ifndef PCX_H
#define PCX_H

#include	"file.h"

/* RGB triplet used in PCX palettes: must be exactly 3 bytes (no padding). */
#pragma pack(push, 1)
typedef	struct {
	char	red;
	char	green;
	char	blue;
} RGB;
#pragma pack(pop)
static_assert(sizeof(RGB) == 3, "RGB must be 3 bytes for PCX palettes");

/* ZSoft PCX: bytes 4–11 are Window = Xmin, Ymin, Xmax, Ymax (inclusive coords).
 * Packed so struct layout matches the 128-byte file header (no padding). */
#pragma pack(push, 1)
typedef	struct {
	char      id;
	char	   version;
	char	   encoding;
	char	   pixelsize;
	short 	   x;        /* Xmin, left */
	short		y;        /* Ymin, top */
	short     x_end;     /* Xmax, right (inclusive) */
	short		y_end;    /* Ymax, bottom (inclusive) */
	short 	   xres;
	short		yres;
	RGB       ega_palette[16];
	char	   nothing;
	char      color_planes;
	short 		byte_per_line;
	short 	 	palette_type;
	char	   filler[58];
} PCX_HEADER;
#pragma pack(pop)

GraphicBufferClass* Read_PCX_File(char* name, char* palette = NULL, void *buff = NULL, long size = 0);
GraphicBufferClass* Read_PCX_File(char* name, BufferClass& Buff, char* palette = NULL);
int Write_PCX_File(char* name, GraphicViewPortClass& pic, unsigned char* palette);

#endif /* PCX_H */

