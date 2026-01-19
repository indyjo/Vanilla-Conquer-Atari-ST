/*
 * drawbuff.cpp - Drawing buffer functions for Atari ST/MiNT
 * 
 * This provides portable C implementations of buffer pixel operations
 */

#include "drawbuff.h"
#include "gbuffer.h"
#include <string.h>  // For memset

/*=========================================================================*/
/* Buffer_Put_Pixel -- Puts a pixel on a graphic viewport                  */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly        */
/*                                                                         */
/* INPUT:                                                                  */
/*   thisptr  -- Pointer to GraphicViewPortClass object                    */
/*   x        -- X position of pixel                                      */
/*   y        -- Y position of pixel                                      */
/*   color    -- Color value to set                                       */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   None                                                                  */
/*=========================================================================*/
extern "C" void Buffer_Put_Pixel(void *thisptr, int x, int y, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Verify bounds
	if (x < 0 || x >= vp->Get_Width()) return;
	if (y < 0 || y >= vp->Get_Height()) return;
	
	// Calculate pixel offset
	long offset = vp->Get_Offset();
	offset += x;
	offset += y * (vp->Get_Pitch() + vp->Get_XAdd());
	
	// Get buffer pointer
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (!gb) return;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return;
	
	// Write pixel
	buffer[offset] = color;
}

/***************************************************************************
 * Fat_Put_Pixel -- Draws a fat pixel (larger than 1x1)                    *
 *                                                                         *
 * INPUT:   x, y - coordinates of upper left corner                        *
 *          color - color value                                            *
 *          siz - size of pixel (square)                                  *
 *          gpage - graphic viewport reference                            *
 *                                                                         *
 * OUTPUT:  none                                                           *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to C)                 *
 *=========================================================================*/
extern "C" void Fat_Put_Pixel(int x, int y, int color, int siz, GraphicViewPortClass &gpage)
{
	if (siz <= 0) return;
	
	// Verify bounds
	if (y < 0 || y >= gpage.Get_Height()) return;
	if (x < 0 || x >= gpage.Get_Width()) return;
	
	// Calculate pixel offset
	long offset = gpage.Get_Offset();
	offset += x;
	offset += y * (gpage.Get_Pitch() + gpage.Get_XAdd());
	
	// Get buffer pointer
	GraphicBufferClass *gb = gpage.Get_Graphic_Buffer();
	if (!gb) return;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return;
	
	// Calculate row stride
	int row_stride = gpage.Get_Pitch() + gpage.Get_XAdd();
	
	// Draw fat pixel (square)
	unsigned char color_byte = (unsigned char)color;
	for (int row = 0; row < siz && (y + row) < gpage.Get_Height(); row++) {
		for (int col = 0; col < siz && (x + col) < gpage.Get_Width(); col++) {
			buffer[offset + col] = color_byte;
		}
		offset += row_stride;
	}
}

/*=========================================================================*/
/* Buffer_Get_Pixel -- Gets a pixel from a graphic viewport                */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly        */
/*                                                                         */
/* INPUT:                                                                  */
/*   thisptr  -- Pointer to GraphicViewPortClass object                    */
/*   x        -- X position of pixel                                      */
/*   y        -- Y position of pixel                                      */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns pixel color value, or 0 if out of bounds                      */
/*=========================================================================*/
extern "C" int Buffer_Get_Pixel(void *thisptr, int x, int y)
{
	if (!thisptr) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Verify bounds
	if (x < 0 || x >= vp->Get_Width()) return 0;
	if (y < 0 || y >= vp->Get_Height()) return 0;
	
	// Calculate pixel offset
	long offset = vp->Get_Offset();
	offset += x;
	offset += y * (vp->Get_Pitch() + vp->Get_XAdd());
	
	// Get buffer pointer
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (!gb) return 0;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return 0;
	
	// Read pixel
	return (int)buffer[offset];
}

/*=========================================================================*/
/* Buffer_Clear -- Clears a buffer to a color                              */
/*=========================================================================*/
extern "C" void Buffer_Clear(void *thisptr, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Get viewport dimensions
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (width <= 0 || height <= 0) return;
	
	// Get buffer pointer
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (!gb) return;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = vp->Get_Pitch() + vp->Get_XAdd();
	
	// Get starting offset
	long offset = vp->Get_Offset();
	
	// Clear each row
	for (int row = 0; row < height; row++) {
		memset(buffer + offset, color, width);
		offset += row_stride;
	}
}

/*=========================================================================*/
/* Buffer_Size_Of_Region -- Gets size of a region                           */
/*=========================================================================*/
extern "C" long Buffer_Size_Of_Region(void *thisptr, int w, int h)
{
	if (!thisptr) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	return vp->Size_Of_Region(w, h);
}

/*=========================================================================*/
/* Buffer_To_Buffer -- Copies buffer region to another buffer               */
/*=========================================================================*/
extern "C" long Buffer_To_Buffer(void *thisptr, int x, int y, int w, int h, void *buff, long size)
{
	if (!thisptr || !buff) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	BufferClass *dest_buff = (BufferClass *)buff;
	
	return vp->To_Buffer(x, y, w, h, dest_buff);
}

/*=========================================================================*/
/* Buffer_To_Page -- Copies buffer to page/viewport                        */
/*=========================================================================*/
extern "C" long Buffer_To_Page(int x, int y, int w, int h, void *Buffer, void *view)
{
	// Stub implementation
	(void)x; (void)y; (void)w; (void)h; (void)Buffer; (void)view;
	return 0;
}

/*=========================================================================*/
/* Linear_Blit_To_Linear -- Blits between linear buffers                   */
/*=========================================================================*/
extern "C" BOOL Linear_Blit_To_Linear(void *thisptr, void *dest, int x_pixel, int y_pixel, int dx_pixel,
							int dy_pixel, int pixel_width, int pixel_height, BOOL trans)
{
	if (!thisptr || !dest) return FALSE;
	
	GraphicViewPortClass *src_vp = (GraphicViewPortClass *)thisptr;
	GraphicViewPortClass *dest_vp = (GraphicViewPortClass *)dest;
	
	// Get buffer pointers and dimensions
	GraphicBufferClass *src_gb = src_vp->Get_Graphic_Buffer();
	GraphicBufferClass *dest_gb = dest_vp->Get_Graphic_Buffer();
	if (!src_gb || !dest_gb) return FALSE;
	
	unsigned char *src_buffer = (unsigned char *)src_gb->Get_Buffer();
	unsigned char *dest_buffer = (unsigned char *)dest_gb->Get_Buffer();
	if (!src_buffer || !dest_buffer) return FALSE;
	
	// Calculate source and destination offsets
	long src_offset = src_vp->Get_Offset() + (src_vp->Get_Pitch() + src_vp->Get_XAdd()) * y_pixel + x_pixel;
	long dest_offset = dest_vp->Get_Offset() + (dest_vp->Get_Pitch() + dest_vp->Get_XAdd()) * dy_pixel + dx_pixel;
	
	// Calculate source and destination strides
	int src_stride = src_vp->Get_Pitch() + src_vp->Get_XAdd();
	int dest_stride = dest_vp->Get_Pitch() + dest_vp->Get_XAdd();
	
	// Perform the blit
	if (trans) {
		// Transparent blit: skip pixels with value 0
		for (int y = 0; y < pixel_height; y++) {
			for (int x = 0; x < pixel_width; x++) {
				unsigned char pixel = src_buffer[src_offset + x];
				if (pixel != 0) {
					dest_buffer[dest_offset + x] = pixel;
				}
			}
			src_offset += src_stride;
			dest_offset += dest_stride;
		}
	} else {
		// Opaque blit: copy all pixels
		for (int y = 0; y < pixel_height; y++) {
			memcpy(&dest_buffer[dest_offset], &src_buffer[src_offset], pixel_width);
			src_offset += src_stride;
			dest_offset += dest_stride;
		}
	}
	
	return TRUE;
}

/*=========================================================================*/
/* Linear_Scale_To_Linear -- Scales between linear buffers                 */
/*=========================================================================*/
extern "C" BOOL Linear_Scale_To_Linear(void *src, void *dest, int src_x, int src_y, int dst_x, int dst_y,
							int src_w, int src_h, int dst_w, int dst_h, BOOL trans, char *remap)
{
	if (!src || !dest) return FALSE;
	
	GraphicViewPortClass *src_vp = (GraphicViewPortClass *)src;
	GraphicViewPortClass *dest_vp = (GraphicViewPortClass *)dest;
	
	BOOL result = src_vp->Scale(*dest_vp, src_x, src_y, dst_x, dst_y, src_w, src_h, dst_w, dst_h, trans, remap);
	return result;
}

/*=========================================================================*/
/* Buffer_Print -- Prints text to a buffer                                  */
/*=========================================================================*/
extern "C" LONG Buffer_Print(void *thisptr, const char *str, int x, int y, int fcolor, int bcolor)
{
	if (!thisptr || !str) return 0;
	
	// FIXME: This is a stub to prevent infinite recursion.
	// Buffer_Print was calling vp->Print(), which calls Buffer_Print() again.
	// TODO: Implement proper text rendering here using FontPtr and font structures.
	// For now, return 0 to allow the program to continue.
	return 0;
}

/*=========================================================================*/
/* Buffer_Draw_Line -- Draws a line on a buffer                             */
/*=========================================================================*/
extern "C" VOID Buffer_Draw_Line(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Get viewport dimensions
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (width <= 0 || height <= 0) return;
	
	// Clip coordinates to viewport bounds
	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (dx < 0) dx = 0;
	if (dy < 0) dy = 0;
	if (sx >= width) sx = width - 1;
	if (sy >= height) sy = height - 1;
	if (dx >= width) dx = width - 1;
	if (dy >= height) dy = height - 1;
	
	// Get buffer pointer
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (!gb) return;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = vp->Get_Pitch() + vp->Get_XAdd();
	
	// Get starting offset
	long offset = vp->Get_Offset();
	
	// Simple line drawing using Bresenham's algorithm
	int x0 = sx, y0 = sy, x1 = dx, y1 = dy;
	int dx_abs = (x1 > x0) ? (x1 - x0) : (x0 - x1);
	int dy_abs = (y1 > y0) ? (y1 - y0) : (y0 - y1);
	int x_inc = (x1 > x0) ? 1 : -1;
	int y_inc = (y1 > y0) ? 1 : -1;
	
	int x = x0, y = y0;
	
	if (dx_abs >= dy_abs) {
		// More horizontal than vertical
		int error = dx_abs / 2;
		for (int i = 0; i <= dx_abs; i++) {
			if (x >= 0 && x < width && y >= 0 && y < height) {
				long pixel_offset = offset + x + y * row_stride;
				buffer[pixel_offset] = color;
			}
			error -= dy_abs;
			if (error < 0) {
				y += y_inc;
				error += dx_abs;
			}
			x += x_inc;
		}
	} else {
		// More vertical than horizontal
		int error = dy_abs / 2;
		for (int i = 0; i <= dy_abs; i++) {
			if (x >= 0 && x < width && y >= 0 && y < height) {
				long pixel_offset = offset + x + y * row_stride;
				buffer[pixel_offset] = color;
			}
			error -= dx_abs;
			if (error < 0) {
				x += x_inc;
				error += dy_abs;
			}
			y += y_inc;
		}
	}
}

/*=========================================================================*/
/* Buffer_Draw_Rect -- Draws a rectangle on a buffer                       */
/*=========================================================================*/
extern "C" VOID Buffer_Draw_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Clip coordinates to viewport bounds
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (dx >= width) dx = width - 1;
	if (dy >= height) dy = height - 1;
	
	// Check if rectangle is valid
	if (sx > dx || sy > dy) return;
	if (sx >= width || sy >= height || dx < 0 || dy < 0) return;
	
	// Get buffer pointer
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (!gb) return;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = vp->Get_Pitch() + vp->Get_XAdd();
	
	// Get starting offset
	long base_offset = vp->Get_Offset();
	
	// Draw top and bottom horizontal lines
	int rect_width = dx - sx + 1;
	long top_offset = base_offset + sx + sy * row_stride;
	long bottom_offset = base_offset + sx + dy * row_stride;
	for (int x = 0; x < rect_width; x++) {
		if (sx + x < width) {
			buffer[top_offset + x] = color;
			buffer[bottom_offset + x] = color;
		}
	}
	
	// Draw left and right vertical lines
	int rect_height = dy - sy + 1;
	for (int y = 0; y < rect_height; y++) {
		if (sy + y < height) {
			long left_offset = base_offset + sx + (sy + y) * row_stride;
			long right_offset = base_offset + dx + (sy + y) * row_stride;
			buffer[left_offset] = color;
			buffer[right_offset] = color;
		}
	}
}

/*=========================================================================*/
/* Buffer_Fill_Rect -- Fills a rectangle on a buffer                       */
/*=========================================================================*/
extern "C" VOID Buffer_Fill_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Clip coordinates to viewport bounds
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (dx >= width) dx = width - 1;
	if (dy >= height) dy = height - 1;
	
	// Check if rectangle is valid
	if (sx > dx || sy > dy) return;
	if (sx >= width || sy >= height || dx < 0 || dy < 0) return;
	
	// Get buffer pointer
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (!gb) return;
	
	unsigned char *buffer = (unsigned char *)gb->Get_Buffer();
	if (!buffer) return;
	
	// Calculate dimensions
	int rect_width = dx - sx + 1;
	int rect_height = dy - sy + 1;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = vp->Get_Pitch() + vp->Get_XAdd();
	
	// Get starting offset
	long offset = vp->Get_Offset();
	offset += sx;
	offset += sy * row_stride;
	
	// Fill each row
	for (int row = 0; row < rect_height; row++) {
		memset(buffer + offset, color, rect_width);
		offset += row_stride;
	}
}

/*=========================================================================*/
/* Buffer_Remap -- Remaps colors in a buffer region                        */
/*=========================================================================*/
extern "C" VOID Buffer_Remap(void *thisptr, int sx, int sy, int width, int height, void *remap)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Remap(sx, sy, width, height, remap);
}

/*=========================================================================*/
/* Buffer_Fill_Quad -- Fills a quadrilateral on a buffer                   */
/*=========================================================================*/
extern "C" VOID Buffer_Fill_Quad(void *thisptr, VOID *span_buff, int x0, int y0, int x1, int y1,
						int x2, int y2, int x3, int y3, int color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Fill_Quad(span_buff, x0, y0, x1, y1, x2, y2, x3, y3, color);
}

/*=========================================================================*/
/* Buffer_Draw_Stamp -- Draws a stamp/icon on a buffer                     */
/*=========================================================================*/
extern "C" void Buffer_Draw_Stamp(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Draw_Stamp(icondata, icon, x_pixel, y_pixel, remap);
}

/*=========================================================================*/
/* Buffer_Draw_Stamp_Clip -- Draws a stamp/icon with clipping              */
/*=========================================================================*/
extern "C" void Buffer_Draw_Stamp_Clip(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int, int, int, int)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Draw_Stamp(icondata, icon, x_pixel, y_pixel, remap);
}

