/*
 * drawbuff.h - Drawing buffer header for Atari ST/MiNT
 */

#ifndef DRAWBUFF_H
#define DRAWBUFF_H

#include "gbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================*/
/* Buffer functions - C interface for GraphicViewPortClass operations     */
/*=========================================================================*/

/* Buffer pixel operations */
void Buffer_Put_Pixel(void *thisptr, int x, int y, unsigned char color);
int Buffer_Get_Pixel(void *thisptr, int x, int y);

/* Other buffer operations */
long Buffer_Size_Of_Region(void *thisptr, int w, int h);
void Buffer_Clear(void *thisptr, unsigned char color);
long Buffer_To_Buffer(void *thisptr, int x, int y, int w, int h, void *buff, long size);
long Buffer_To_Page(int x, int y, int w, int h, void *Buffer, void *view);
BOOL Linear_Blit_To_Linear(void *thisptr, void *dest, int x_pixel, int y_pixel, int dx_pixel,
							int dy_pixel, int pixel_width, int pixel_height, BOOL trans);
BOOL Linear_Scale_To_Linear(void *, void *, int, int, int, int, int, int, int, int, BOOL, char *);
LONG Buffer_Print(void *thisptr, const char *str, int x, int y, int fcolor, int bcolor);

/* Graphic buffer class only functions */
VOID Buffer_Draw_Line(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color);
VOID Buffer_Draw_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color);
VOID Buffer_Fill_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color);
VOID Buffer_Remap(void *thisptr, int sx, int sy, int width, int height, void *remap);
VOID Buffer_Fill_Quad(void *thisptr, VOID *span_buff, int x0, int y0, int x1, int y1,
						int x2, int y2, int x3, int y3, int color);
void Buffer_Draw_Stamp(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap);
void Buffer_Draw_Stamp_Clip(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int, int, int, int);
void Fat_Put_Pixel(int x, int y, int color, int siz, GraphicViewPortClass &gpage);

#ifdef __cplusplus
}
#endif

/*=========================================================================*/
/* Inline C++ overload for Buffer_To_Page (takes reference instead of pointer) */
/*=========================================================================*/
#ifdef __cplusplus
inline long Buffer_To_Page(int x, int y, int w, int h, void *Buffer, GraphicViewPortClass &view)
{
	long	return_code=0;
	if (view.Lock()){
		return_code = (Buffer_To_Page(x, y, w, h, Buffer, &view));
	}
	view.Unlock();
	return ( return_code );
}
#endif

extern GraphicViewPortClass *LogicPage;
extern BOOL AllowHardwareBlitFills;

#endif /* DRAWBUFF_H */

