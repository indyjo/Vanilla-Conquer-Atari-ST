/*
 * writepcx.cpp - Write PCX file for Atari ST/MiNT
 *
 * Port of WIN32LIB/WRITEPCX.CPP using RawFileClass for file I/O.
 */

#include "gbuffer.h"    /* GraphicViewPortClass, GraphicBufferClass (before filepcx) */
#include "filepcx.h"
#include <stdio.h>      /* SEEK_CUR for RAWFILE/wwfile */
#include "../RAWFILE.H"  /* root: full RawFileClass; ATARILIB/rawfile.h is a stub */
#include "memflag.h"
#include <string.h>

/* Write a single RLE-encoded scanline to the file */
static void Write_Pcx_ScanLine(RawFileClass& file, int scansize, unsigned char* ptr)
{
	unsigned int i;
	unsigned int rle;
	unsigned int color;
	unsigned int last;
	unsigned char pool[2048];
	unsigned char* file_ptr = pool;
	const unsigned int POOL_SIZE = sizeof(pool);

#define WRITE_CHAR(x) do { \
		*file_ptr++ = (unsigned char)(x); \
		if (file_ptr >= pool + POOL_SIZE) { \
			file.Write(pool, POOL_SIZE); \
			file_ptr = pool; \
		} \
	} while (0)

	file_ptr = pool;
	last = *ptr;
	rle = 1;

	for (i = 1; i < (unsigned)scansize; i++) {
		color = 0xff & *++ptr;
		if (color == last) {
			rle++;
			if (rle == 63) {
				WRITE_CHAR(255);
				WRITE_CHAR(color);
				rle = 0;
			}
		} else {
			if (rle) {
				if (rle == 1 && (192 != (192 & last))) {
					WRITE_CHAR(last);
				} else {
					WRITE_CHAR(rle | 192);
					WRITE_CHAR(last);
				}
			}
			last = color;
			rle = 1;
		}
	}
	if (rle) {
		if (rle == 1 && (192 != (192 & last))) {
			WRITE_CHAR(last);
		} else {
			WRITE_CHAR(rle | 192);
			WRITE_CHAR(last);
		}
	}

	file.Write(pool, (long)(file_ptr - pool));
#undef WRITE_CHAR
}

/* Write a 16-bit value as little-endian bytes to buffer */
static void PutLE16(unsigned char* buf, int offset, short val) {
	buf[offset] = (unsigned char)(val & 0xFF);
	buf[offset + 1] = (unsigned char)((val >> 8) & 0xFF);
}

int Write_PCX_File(char* name, GraphicViewPortClass& pic, unsigned char* palette)
{
	unsigned char palcopy[256 * 3];
	unsigned int i;
	int VP_Scan_Line;
	int width, height;
	int bytes_per_line;  /* PCX requires even bytes per line */
	unsigned char* ptr;
	GraphicBufferClass* Graphic_Buffer;
	/* Temp line for padding: PCX scanline must decode to bytes_per_line bytes (even) */
	unsigned char line_buf[640];
	unsigned char header_buf[128];  /* PCX header is 128 bytes */

	width = pic.Get_Width();
	height = pic.Get_Height();
	bytes_per_line = (width + 1) & ~1;  /* round up to even per PCX spec */

	/* Build header byte-by-byte in little-endian format */
	memset(header_buf, 0, sizeof(header_buf));
	header_buf[0] = 10;      /* id */
	header_buf[1] = 5;       /* version */
	header_buf[2] = 1;       /* encoding */
	header_buf[3] = 8;       /* pixelsize */
	/* x, y, width, height, xres, yres are 16-bit little-endian */
	PutLE16(header_buf, 4, 0);                    /* x = 0 */
	PutLE16(header_buf, 6, 0);                    /* y = 0 */
	PutLE16(header_buf, 8, (short)(width - 1));   /* width */
	PutLE16(header_buf, 10, (short)(height - 1));  /* height */
	PutLE16(header_buf, 12, 320);                  /* xres */
	PutLE16(header_buf, 14, 200);                  /* yres */
	/* ega_palette[16] = 48 bytes of zeros (already memset) */
	header_buf[64] = 0;      /* nothing/reserved */
	header_buf[65] = 1;     /* color_planes */
	/* byte_per_line and palette_type are 16-bit little-endian */
	PutLE16(header_buf, 66, (short)bytes_per_line);
	PutLE16(header_buf, 68, 1);                    /* palette_type */
	/* filler[58] = zeros (already memset) */

	RawFileClass file(name);
	if (!file.Open(WRITE))
		return -1;

	/* Write header byte-by-byte in little-endian format */
	file.Write(header_buf, 128);

	Graphic_Buffer = pic.Get_Graphic_Buffer();
	if (!Graphic_Buffer)
		return -1;
	/* Use buffer's physical row stride so it matches decode (Get_Width + Get_Pitch) */
	VP_Scan_Line = Graphic_Buffer->Get_Width() + Graphic_Buffer->Get_Pitch();
	ptr = (unsigned char*)Graphic_Buffer->Get_Buffer();
	if (!ptr)
		return -1;
	/* Offset to viewport origin within buffer */
	ptr += (pic.Get_YPos() * (long)VP_Scan_Line) + pic.Get_XPos();

	for (i = 0; i < (unsigned)height; i++) {
		/* Copy one scanline: advance by buffer row stride */
		Mem_Copy(ptr + i * (long)VP_Scan_Line, line_buf, (unsigned long)width);
		if (width < bytes_per_line)
			memset(line_buf + width, 0, (size_t)(bytes_per_line - width));
		Write_Pcx_ScanLine(file, bytes_per_line, line_buf);
	}

	/* Palette: copy; scale 6-bit to 8-bit only if input is 6-bit (max component <= 63) */
	Mem_Copy(palette, palcopy, 256 * 3);
	{
		unsigned int need_scale = 1;
		for (i = 0; i < 256 * 3 && need_scale; i++)
			if (palcopy[i] > 63) need_scale = 0;
		if (need_scale)
			for (i = 0; i < 256; i++) {
				palcopy[i * 3 + 0] = (unsigned char)((palcopy[i * 3 + 0] << 2) & 0xff);
				palcopy[i * 3 + 1] = (unsigned char)((palcopy[i * 3 + 1] << 2) & 0xff);
				palcopy[i * 3 + 2] = (unsigned char)((palcopy[i * 3 + 2] << 2) & 0xff);
			}
	}
	{
		unsigned char pcx_pal_marker = 0x0c;
		file.Write(&pcx_pal_marker, 1);
	}
	file.Write(palcopy, 256 * sizeof(RGB));

	file.Close();
	return 0;
}
