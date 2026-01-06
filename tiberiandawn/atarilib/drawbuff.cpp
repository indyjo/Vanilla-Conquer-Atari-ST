/*
 * drawbuff.cpp - Drawing buffer functions for Atari ST/MiNT
 * 
 * This provides portable C implementations of buffer pixel operations
 */

#include "drawbuff.h"
#include "gbuffer.h"

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
	vp->Clear(color);
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
	
	HRESULT result = src_vp->Blit(*dest_vp, x_pixel, y_pixel, dx_pixel, dy_pixel, pixel_width, pixel_height, trans);
	return (result == 0) ? TRUE : FALSE;
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
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	return vp->Print(str, x, y, fcolor, bcolor);
}

/*=========================================================================*/
/* Buffer_Draw_Line -- Draws a line on a buffer                             */
/*=========================================================================*/
extern "C" VOID Buffer_Draw_Line(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Draw_Line(sx, sy, dx, dy, color);
}

/*=========================================================================*/
/* Buffer_Draw_Rect -- Draws a rectangle on a buffer                       */
/*=========================================================================*/
extern "C" VOID Buffer_Draw_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Draw_Rect(sx, sy, dx, dy, color);
}

/*=========================================================================*/
/* Buffer_Fill_Rect -- Fills a rectangle on a buffer                       */
/*=========================================================================*/
extern "C" VOID Buffer_Fill_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	vp->Fill_Rect(sx, sy, dx, dy, color);
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

