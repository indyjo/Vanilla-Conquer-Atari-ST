/*
 * gbuffer.cpp - Graphic buffer implementation for Atari ST/MiNT
 * 
 * This provides implementations for GraphicViewPortClass methods
 * that are compatible with WIN32LIB/gbuffer.cpp
 */

#include "gbuffer.h"
#include "drawbuff.h"  // For Buffer_Clear, Buffer_Fill_Quad
#include "c2p.h"
#include "ww_win.h"
#include <stdio.h>  // For sprintf

/***************************************************************************
 * GVPC::GRAPHICVIEWPORTCLASS -- Constructor for basic view port class     *
 *                                                                         *
 * INPUT:		GraphicBufferClass * gbuffer	- buffer to attach to         *
 *					int x								- x offset into buffer          *
 *					int y								- y offset into buffer          *
 *					int w								- view port width in pixels     *
 *					int h   							- view port height in pixels    *
 *                                                                         *
 * OUTPUT:     Constructors may not have a return value                    *
 *                                                                         *
 * HISTORY:                                                                *
 *   05/09/1994 PWG : Created.                                             *
 *=========================================================================*/
GraphicViewPortClass::GraphicViewPortClass(GraphicBufferClass *gbuffer, int x, int y, int w, int h) :
	LockCount(0),
	GraphicBuff(NULL)
{
	Attach(gbuffer, x, y, w, h);
}

/***************************************************************************
 * GVPC::GRAPHICVIEWPORTCLASS -- Default constructor for view port class   *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   05/09/1994 PWG : Created.                                             *
 *=========================================================================*/
GraphicViewPortClass::GraphicViewPortClass() :
	Offset(0),
	Width(0),
	Height(0),
	XAdd(0),
	XPos(0),
	YPos(0),
	Pitch(0),
	GraphicBuff(NULL),
	IsDirectDraw(FALSE),
	LockCount(0)
{
}

/***************************************************************************
 * GVPC::~GRAPHICVIEWPORTCLASS -- Destructor for GraphicViewPortClass     *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     A destructor may not return a value.                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   05/10/1994 PWG : Created.                                             *
 *=========================================================================*/
GraphicViewPortClass::~GraphicViewPortClass(void)
{
 	Offset			= 0;
	Width				= 0;										// Record width of Buffer
	Height			= 0;										// Record height of Buffer
	XAdd				= 0;										// Record XAdd of Buffer
	XPos				= 0;										// Record XPos of Buffer
	YPos				= 0;										// Record YPos of Buffer
	Pitch				= 0;										// Record width of Buffer
	IsDirectDraw	= FALSE;
	LockCount		= 0;
	GraphicBuff		= NULL;
}

// Global LogicPage pointer - used by Set_Logic_Page
GraphicViewPortClass *LogicPage = NULL;

/***************************************************************************
 * SET_LOGIC_PAGE -- Sets LogicPage to new buffer                          *
 *                                                                         *
 * INPUT:		GraphicViewPortClass *ptr - the buffer we are going to set  *
 *                                                                         *
 * OUTPUT:     GraphicViewPortClass * - the previous buffer               *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicViewPortClass *Set_Logic_Page(GraphicViewPortClass *ptr)
{
	GraphicViewPortClass *old = LogicPage;
	LogicPage = ptr;
	return(old);
}

/***************************************************************************
 * SET_LOGIC_PAGE -- Sets LogicPage to new buffer                          *
 *                                                                         *
 * INPUT:		GraphicViewPortClass &ptr - the buffer we are going to set  *
 *                                                                         *
 * OUTPUT:     GraphicViewPortClass * - the previous buffer               *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicViewPortClass *Set_Logic_Page(GraphicViewPortClass &ptr)
{
	GraphicViewPortClass *old = LogicPage;
	LogicPage = &ptr;
	return(old);
}

/***************************************************************************
 * GVPC::ATTACH -- Attaches a viewport to a buffer class                   *
 *                                                                         *
 * INPUT:		GraphicBufferClass *g_buff	- pointer to gbuff to attach to  *
 *					int x                     - x position to attach to			*
 *					int y 							- y position to attach to			*
 *					int w							- width of the view port			*
 *					int h							- height of the view port			*
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   05/10/1994 PWG : Created.                                             *
 *=========================================================================*/
void GraphicViewPortClass::Attach(GraphicBufferClass *gbuffer, int x, int y, int w, int h)
{
	/*======================================================================*/
	/* Can not attach a Graphic View Port if it is actually the physical		*/
	/*	   representation of a Graphic Buffer.											*/
	/*======================================================================*/
	if (this == Get_Graphic_Buffer())  {
		return;
	}

	/*======================================================================*/
	/* Protect against NULL gbuffer - would cause bus error when accessing	*/
	/*		members.																					*/
	/*======================================================================*/
	if (!gbuffer) {
		// If gbuffer is NULL, just initialize to safe defaults
		Offset = 0;
		Width = w;
		Height = h;
		XAdd = 0;
		XPos = x;
		YPos = y;
		Pitch = 0;
		GraphicBuff = NULL;
		IsDirectDraw = FALSE;
		return;
	}

	/*======================================================================*/
	/* Verify that the x and y coordinates are valid and placed within the	*/
	/*		physical buffer.																	*/
	/*======================================================================*/
	if (x < 0) 										// you cannot place view port off
		x = 0;										//		the left edge of physical buf
	if (x >= gbuffer->Get_Width())			// you cannot place left edge off
		x = gbuffer->Get_Width() - 1;			//		the right edge of physical buf
	if (y < 0) 										// you cannot place view port off
		y = 0;										//		the top edge of physical buf
	if (y >= gbuffer->Get_Height()) 			// you cannot place view port off
		y = gbuffer->Get_Height() - 1;		//		bottom edge of physical buf

	/*======================================================================*/
	/* Adjust the width and height of necessary										*/
	/*======================================================================*/
	if (x + w > gbuffer->Get_Width()) 		// if the x plus width is larger
		w = gbuffer->Get_Width() - x;			//		than physical, fix width

	if (y + h > gbuffer->Get_Height()) 		// if the y plus height is larger
		h = gbuffer->Get_Height() - y;		//		than physical, fix height

	/*======================================================================*/
	/* Get a pointer to the top left edge of the buffer.							*/
	/*======================================================================*/
	if (gbuffer->Uses_ST_LoRes_Planar_Layout()) {
		/* Planar: Offset is buffer root; all pixels use (XPos+x, YPos+y) in drawbuff. */
		Offset = gbuffer->Get_Offset();
	} else {
		Offset = gbuffer->Get_Offset() + ((gbuffer->Get_Width()+gbuffer->Get_Pitch()) * y) + x;
	}

	/*======================================================================*/
	/* Copy over all of the variables that we need to store.						*/
	/*======================================================================*/
 	XPos			= x;
 	YPos			= y;
 	XAdd			= gbuffer->Get_Width() - w;
 	Width			= w;
 	Height		= h;
	if (gbuffer->Uses_ST_LoRes_Planar_Layout()) {
		Pitch = ST_PLANAR_BYTES_PER_LINE;
	} else {
		// On Atari, backing buffers use Pitch==0 to mean 'no padding; row stride = Width'.
		// Viewports, however, use (Pitch + XAdd) as the per-scanline stride. If we simply
		// copy a zero Pitch here, full-screen viewports end up with stride 0 and only the
		// first line ever gets updated. When the backing buffer has Pitch==0, treat its
		// logical row stride as its Width for viewport Pitch.
		int buf_pitch = gbuffer->Get_Pitch();
		Pitch = buf_pitch ? buf_pitch : gbuffer->Get_Width();
	}
 	GraphicBuff = gbuffer;
	IsDirectDraw= gbuffer->Get_IsDirectDraw();
}

/***************************************************************************
 * GVPC::ATTACH -- Attaches a viewport to a buffer class (convenience)     *
 *                                                                         *
 * INPUT:		GraphicBufferClass *video_buff	- pointer to buffer to attach to *
 *					int w							- width of the view port			*
 *					int h							- height of the view port			*
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Convenience wrapper that calls the main Attach with x=0, y=0          *
 *=========================================================================*/
void GraphicViewPortClass::Attach(GraphicBufferClass *video_buff, int w, int h)
{
	Attach(video_buff, 0, 0, w, h);
}

/***************************************************************************
 * GVPC::CLEAR -- Clears a viewport to a color                             *
 *                                                                         *
 * INPUT:		unsigned char color - the color to clear to (default 0)    *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   01/06/1995 PWG : Created.                                             *
 *=========================================================================*/
void GraphicViewPortClass::Clear(unsigned char color)
{
	if (Lock()){
		Buffer_Clear(this, color);
	}
	Unlock();
}

/***************************************************************************
 * GBC::INIT -- Core function responsible for initing a GBC                *
 *                                                                         *
 * INPUT:		int 		- the width in pixels of the GraphicBufferClass    *
 *					int		- the height in pixels of the GraphicBufferClass		*
 *					void *	- pointer to user supplied buffer (system will		*
 *								  allocate space if buffer is NULL)						*
 *					long		- size of the user provided buffer						*
 *					int		- flags if this is defined as a special buffer			*
 *	                       (GBC_VIDEOMEM, GBC_VISIBLE, etc.)							*
 *                                                                         *
 * OUTPUT:		none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   10/09/1995     : Created.                                             *
 *=========================================================================*/
void GraphicBufferClass::Init(int w, int h, void *buffer, long size, int flags)
{
	VideoSurfacePtr	= NULL;
	Size			= size;									// find size of physical buffer
	Width			= w;										// Record width of Buffer
	Height		= h;										// Record height of Buffer
	SurfaceFormat	= (flags & GBC_ST_PLANAR_LORES) ? 1 : 0;

	//
	// For Atari ST, we don't have DirectDraw, so we always do normal allocation
	//
	if (buffer) {										// if buffer is specified
		Buffer		= (unsigned char *)buffer;		//		point to it and mark
		Allocated	= FALSE;							//		it as user allocated
	} else {
		if (SurfaceFormat) {
			if (!Size)
				Size = 32768; /* room for 320x200 planar + alignment headroom */
		} else {
			if (!Size) Size = w*h;
		}
		Buffer		= new unsigned char[Size];		// otherwise allocate it and
		Allocated	= TRUE;							//		mark it system alloced
	}
	Offset			= (long)Buffer;				// Get offset to the buffer
	IsDirectDraw	= FALSE;

	if (SurfaceFormat) {
		Pitch = ST_PLANAR_BYTES_PER_LINE;
	} else {
		Pitch			= 0;								// No padding; row stride = Width
	}
	XAdd			= 0;										// Record XAdd of Buffer
	XPos			= 0;										// Record XPos of Buffer
	YPos			= 0;										// Record YPos of Buffer
	GraphicBuff	= this;									// Get a pointer to our self
}

/***************************************************************************
 * GBC::USES_ST_LORES_PLANAR_LAYOUT -- ST 320×200 planar surface detection   *
 ***************************************************************************/
BOOL GraphicBufferClass::Uses_ST_LoRes_Planar_Layout(void) const
{
	if (Is_ST_Planar())
		return TRUE;
	if (Width != ST_PLANAR_WIDTH || Height != ST_PLANAR_HEIGHT)
		return FALSE;
	if (Pitch != ST_PLANAR_BYTES_PER_LINE)
		return FALSE;
	if (Size < (long)ST_PLANAR_SCREEN_BYTES || Size > 65536L)
		return FALSE;
	return TRUE;
}

/***************************************************************************
 * GBC::SET_LINEAR_ROW_PADDING_BYTES -- bytes after each row's Width pixels *
 ***************************************************************************/
void GraphicBufferClass::Set_Linear_Row_Padding_Bytes(int padding_after_width)
{
	if (Is_ST_Planar())
		return;
	if (padding_after_width < 0)
		padding_after_width = 0;
	Pitch = padding_after_width;
}

/***************************************************************************
 * GBC::SWAP_PLANAR_BUFFER_WITH -- Swap backing memory with peer buffer     *
 ***************************************************************************/
void GraphicBufferClass::Swap_Planar_Buffer_With(GraphicBufferClass &other)
{
	if (!Is_ST_Planar() || !other.Is_ST_Planar())
		return;
	if (Width != other.Width || Height != other.Height || Size != other.Size)
		return;
	unsigned char *tmp = (unsigned char *)Buffer;
	Buffer = other.Buffer;
	other.Buffer = tmp;
	Offset = (long)Buffer;
	other.Offset = (long)other.Buffer;
}

/***************************************************************************
 * GBC::LOCK -- Locks a graphic buffer for access                          *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     BOOL - TRUE if lock successful                             *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST - always succeeds                                  *
 *=========================================================================*/
BOOL GraphicBufferClass::Lock(void)
{
	// For Atari ST, we don't have DirectDraw, so locking always succeeds
	LockCount++;
	return(TRUE);
}

/***************************************************************************
 * GBC::UNLOCK -- Unlocks a graphic buffer                                 *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     BOOL - TRUE if unlock successful                           *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST - always succeeds                                  *
 *=========================================================================*/
BOOL GraphicBufferClass::Unlock(void)
{
	// For Atari ST, we don't have DirectDraw, so unlocking always succeeds
	if (LockCount > 0) {
		LockCount--;
	}
	return(TRUE);
}

// Global variable for hardware blit fills (stub for Atari ST)
BOOL AllowHardwareBlitFills = TRUE;

/***************************************************************************
 * GVPC::DRAW_STAMP -- stub function to draw a tile on a graphic view port *
 *                                                                         *
 * INPUT:		void const *icondata - pointer to icon data                *
 *					int icon - icon number to draw                              *
 *					int x_pixel - x coordinate                                  *
 *					int y_pixel - y coordinate                                  *
 *					void const *remap - remap table (optional)                 *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                     *
 *=========================================================================*/
void GraphicViewPortClass::Draw_Stamp(void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap)
{
	if (Lock()){
		Buffer_Draw_Stamp(this, icondata, icon, x_pixel, y_pixel, remap);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::DRAW_STAMP -- stub function to draw a tile with clipping          *
 *                                                                         *
 * INPUT:		void const *icondata - pointer to icon data                *
 *					int icon - icon number to draw                              *
 *					int x_pixel - x coordinate                                  *
 *					int y_pixel - y coordinate                                  *
 *					void const *remap - remap table (optional)                 *
 *					int clip_window - window index for clipping                *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                     *
 *=========================================================================*/
void GraphicViewPortClass::Draw_Stamp(void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int clip_window)
{
	GraphicViewPortClass draw_window(
		Get_Graphic_Buffer(),
		WindowList[clip_window][WINDOWX] + Get_XPos(),
		WindowList[clip_window][WINDOWY] + Get_YPos(),
		WindowList[clip_window][WINDOWWIDTH],
		WindowList[clip_window][WINDOWHEIGHT]);

	if (draw_window.Get_Width() <= 0 || draw_window.Get_Height() <= 0) {
		return;
	}

	if (draw_window.Lock()){
		Buffer_Draw_Stamp(&draw_window, icondata, icon, x_pixel, y_pixel, remap);
	}
	draw_window.Unlock();
}

/***************************************************************************
 * GVPC::FILL_QUAD -- Stub function to fill quadrilateral in a GVPC        *
 *                                                                         *
 * INPUT:		VOID *span_buff - span buffer                               *
 *					int x0, y0, x1, y1, x2, y2, x3, y3 - quadrilateral corners *
 *					int color - color to fill with                            *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
VOID GraphicViewPortClass::Fill_Quad(VOID *span_buff, int x0, int y0, int x1, int y1,
						int x2, int y2, int x3, int y3, int color)
{
	if (Lock()){
		Buffer_Fill_Quad(this, span_buff, x0, y0, x1, y1, x2, y2, x3, y3, color);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::REMAP -- Stub function to remap a GVPC                            *
 *                                                                         *
 * INPUT:		int sx, sy - start coordinates                             *
 *					int width, height - region size                           *
 *					VOID *remap - remap table                                 *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
VOID GraphicViewPortClass::Remap(int sx, int sy, int width, int height, VOID *remap)
{
	if (Lock()){
		Buffer_Remap(this, sx, sy, width, height, remap);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::REMAP -- Short form to remap an entire graphic view port          *
 *                                                                         *
 * INPUT:		VOID *remap - remap table                                   *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
VOID GraphicViewPortClass::Remap(VOID *remap)
{
	if (Lock()){
		Buffer_Remap(this, 0, 0, Width, Height, remap);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::FILL_RECT -- Stub function to fill rectangle in a GVPC            *
 *                                                                         *
 * INPUT:		int sx, sy - start coordinates                             *
 *					int dx, dy - end coordinates                               *
 *					unsigned char color - color to fill with                   *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h (simplified for Atari ST)            *
 *=========================================================================*/
VOID GraphicViewPortClass::Fill_Rect(int sx, int sy, int dx, int dy, unsigned char color)
{
	if (Lock()){
		Buffer_Fill_Rect(this, sx, sy, dx, dy, color);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::DRAW_LINE -- Stub function to draw line in Graphic Viewport Class *
 *                                                                         *
 * INPUT:		int sx, sy - start coordinates                             *
 *					int dx, dy - end coordinates                               *
 *					unsigned char color - color to draw with                   *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
VOID GraphicViewPortClass::Draw_Line(int sx, int sy, int dx, int dy, unsigned char color)
{
	if (Lock()){
		Buffer_Draw_Line(this, sx, sy, dx, dy, color);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::DRAW_RECT -- Stub function to draw rectangle in a GVPC            *
 *                                                                         *
 * INPUT:		int sx, sy - start coordinates                             *
 *					int dx, dy - end coordinates                               *
 *					unsigned char color - color to draw with                   *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
VOID GraphicViewPortClass::Draw_Rect(int sx, int sy, int dx, int dy, unsigned char color)
{
	if (Lock()){
		Buffer_Draw_Rect(this, sx, sy, dx, dy, color);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::PRINT -- stub func to print a text string                         *
 *                                                                         *
 * INPUT:		char const *str - string to print                           *
 *					int x, y - coordinates                                   *
 *					int fcol, bcol - foreground and background colors        *
 *                                                                         *
 * OUTPUT:     unsigned long - return code                                  *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
unsigned long GraphicViewPortClass::Print(char const *str, int x, int y, int fcol, int bcol)
{
	unsigned long	return_code=0;
	if (Lock()){
		return_code = (Buffer_Print(this, str, x, y, fcol, bcol));
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::PRINT -- Stub function to print an integer                        *
 *                                                                         *
 * INPUT:		int num - number to print                                   *
 *					int x, y - coordinates                                   *
 *					int fcol, bcol - foreground and background colors        *
 *                                                                         *
 * OUTPUT:     unsigned long - return code                                  *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
unsigned long GraphicViewPortClass::Print(int num, int x, int y, int fcol, int bcol)
{
	char str[17];
	unsigned long	return_code=0;
	if (Lock()){
		sprintf(str, "%d", num);
		return_code = (Buffer_Print(this, str, x, y, fcol, bcol));
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::SCALE -- Stub function to scale a region                           *
 *                                                                         *
 * INPUT:		GraphicViewPortClass &dest - destination viewport            *
 *					int src_x, src_y - source coordinates                    *
 *					int dst_x, dst_y - destination coordinates               *
 *					int src_w, src_h - source size                            *
 *					int dst_w, dst_h - destination size                      *
 *					BOOL trans - transparency flag                           *
 *					char *remap - remap table (optional)                      *
 *                                                                         *
 * OUTPUT:     BOOL - success flag                                         *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h (simplified for Atari ST)            *
 *=========================================================================*/
BOOL GraphicViewPortClass::Scale(GraphicViewPortClass &dest, int src_x, int src_y, int dst_x,
						int dst_y, int src_w, int src_h, int dst_w, int dst_h, BOOL trans, char *remap)
{
	BOOL return_code = FALSE;
	if (Lock()){
		if (dest.Lock()){
			return_code = (Linear_Scale_To_Linear(this, &dest, src_x, src_y, dst_x, dst_y, src_w, src_h, dst_w, dst_h, trans, remap));
		}
		dest.Unlock();
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::BLIT -- stub to call curr graphic mode Blit to GVPC               *
 *                                                                         *
 * INPUT:		GraphicViewPortClass &dest - destination viewport            *
 *					int x_pixel, y_pixel - source coordinates                 *
 *					int dx_pixel, dy_pixel - destination coordinates          *
 *					int pixel_width, pixel_height - size                       *
 *					BOOL trans - transparency flag                             *
 *                                                                         *
 * OUTPUT:     HRESULT - return code                                       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h (simplified for Atari ST)            *
 *=========================================================================*/
HRESULT GraphicViewPortClass::Blit(GraphicViewPortClass& dest, int x_pixel, int y_pixel, int dx_pixel,
						int dy_pixel, int pixel_width, int pixel_height, BOOL trans)
{
	HRESULT		return_code=0;
	if (Lock()){
		if (dest.Lock()){
			return_code=(Linear_Blit_To_Linear(this, &dest, x_pixel, y_pixel
														, dx_pixel, dy_pixel
														, pixel_width, pixel_height, trans));
		}
		dest.Unlock();
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::BLIT -- Stub 2 to call curr graphic mode Blit to GVPC            *
 *                                                                         *
 * INPUT:		GraphicViewPortClass &dest - destination viewport            *
 *					int dx, dy - destination coordinates                      *
 *					BOOL trans - transparency flag                             *
 *                                                                         *
 * OUTPUT:     HRESULT - return code                                       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h (simplified for Atari ST)            *
 *=========================================================================*/
HRESULT GraphicViewPortClass::Blit(GraphicViewPortClass& dest, int dx, int dy, BOOL trans)
{
	HRESULT		return_code=0;
	if (Lock()){
		if (dest.Lock()){
			return_code=(Linear_Blit_To_Linear(this, &dest, 0, 0
														, dx, dy
														, Width, Height, trans));
		}
		dest.Unlock();
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::BLIT -- Stub 3 to call curr graphic mode Blit to GVPC            *
 *                                                                         *
 * INPUT:		GraphicViewPortClass &dest - destination viewport            *
 *					BOOL trans - transparency flag                             *
 *                                                                         *
 * OUTPUT:     HRESULT - return code                                       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h (simplified for Atari ST)            *
 *=========================================================================*/
HRESULT GraphicViewPortClass::Blit(GraphicViewPortClass& dest, BOOL trans)
{
	HRESULT		return_code=0;
	if (Lock()){
		if (dest.Lock()){
			return_code=(Linear_Blit_To_Linear(this, &dest, 0, 0
														, 0, 0
														, Width, Height, trans));
		}
		dest.Unlock();
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::TO_BUFFER -- stub to call curr graphic mode To_Buffer             *
 *                                                                         *
 * INPUT:		int x, y - coordinates                                       *
 *					int w, h - size                                             *
 *					void *buff - destination buffer                             *
 *					long size - buffer size                                     *
 *                                                                         *
 * OUTPUT:     long - return code                                          *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
long GraphicViewPortClass::To_Buffer(int x, int y, int w, int h, void *buff, long size)
{
	long	return_code=0;
	if (Lock()){
		return_code = (Buffer_To_Buffer(this, x, y, w, h, buff, size));
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::TO_BUFFER -- stub to call curr graphic mode To_Buffer             *
 *                                                                         *
 * INPUT:		int x, y - coordinates                                       *
 *					int w, h - size                                             *
 *					BufferClass *buff - destination buffer                     *
 *                                                                         *
 * OUTPUT:     long - return code                                          *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
long GraphicViewPortClass::To_Buffer(int x, int y, int w, int h, BufferClass *buff)
{
	long	return_code=0;
	if (Lock()){
		return_code = (Buffer_To_Buffer(this, x, y, w, h, buff->Get_Buffer(), buff->Get_Size()));
	}
	Unlock();
	return ( return_code );
}

/***************************************************************************
 * GVPC::SIZE_OF_REGION -- stub to call curr graphic mode Size_Of_Region  *
 *                                                                         *
 * INPUT:		int w, h - width and height                                  *
 *                                                                         *
 * OUTPUT:     long - size of region                                       *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
long GraphicViewPortClass::Size_Of_Region(int w, int h)
{
	return Buffer_Size_Of_Region(this, w, h);
}

/***************************************************************************
 * GVPC::GET_GRAPHIC_BUFFER -- Get the graphic buffer of the VP            *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     GraphicBufferClass* - pointer to graphic buffer             *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
GraphicBufferClass *GraphicViewPortClass::Get_Graphic_Buffer(void)
{
	return (GraphicBuff);
}

/***************************************************************************
 * GVPC::GET_OFFSET -- Get offset for virtual view port class instance     *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     long - the offset for the virtual viewport instance         *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
long GraphicViewPortClass::Get_Offset(void)
{
	return(Offset);
}

/***************************************************************************
 * GVPC::GET_HEIGHT -- Get the height of a virtual viewport instance       *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - the height of the virtual viewport instance           *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_Height(void)
{
	return(Height);
}

/***************************************************************************
 * GVPC::GET_WIDTH -- Get the width of a virtual viewport instance         *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - the width of the virtual viewport instance            *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_Width(void)
{
	return(Width);
}

/***************************************************************************
 * GVPC::GET_XADD -- Get the X add offset for virtual viewport instance    *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - xadd for a virtual viewport instance                  *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_XAdd(void)
{
	return(XAdd);
}

/***************************************************************************
 * GVPC::GET_XPOS -- Get the x pos of the VP on the Video                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - x offset to VideoBufferClass                          *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_XPos(void)
{
	return(XPos);
}

/***************************************************************************
 * GVPC::GET_YPOS -- Get the y pos of the VP on the video                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - y offset to VideoBufferClass                          *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_YPos(void)
{
	return(YPos);
}

/***************************************************************************
 * GVPC::GET_PITCH -- Get the pitch for virtual viewport instance          *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - pitch for a virtual viewport instance                 *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_Pitch(void)
{
	return(Pitch);
}

/***************************************************************************
 * GBC::~GRAPHICBUFFERCLASS -- Destructor for GraphicBufferClass          *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
/***************************************************************************
 * GBC::GRAPHICBUFFERCLASS -- Constructor for GraphicBufferClass            *
 *                                                                         *
 * INPUT:		int w - width of buffer in pixels                           *
 *					int h - height of buffer in pixels                        *
 *					void *buffer - pointer to buffer (optional)               *
 *					long size - size of buffer                                *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicBufferClass::GraphicBufferClass(int w, int h, void *buffer, long size)
{
	Init(w, h, buffer, size, 0);
}

/***************************************************************************
 * GBC::GRAPHICBUFFERCLASS -- Constructor for GraphicBufferClass            *
 *                                                                         *
 * INPUT:		int w - width of buffer in pixels                           *
 *					int h - height of buffer in pixels                        *
 *					void *buffer - pointer to buffer (optional)               *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicBufferClass::GraphicBufferClass(int w, int h, void *buffer)
{
	Init(w, h, buffer, w * h, 0);
}

/***************************************************************************
 * GBC::GRAPHICBUFFERCLASS -- Constructor for GraphicBufferClass            *
 *                                                                         *
 * INPUT:		int w - width of buffer in pixels                           *
 *					int h - height of buffer in pixels                        *
 *					int flags - flags for creation                            *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicBufferClass::GraphicBufferClass(int w, int h, int flags)
{
	long sz = (flags & GBC_ST_PLANAR_LORES) ? 32768L : (long)(w * h);
	Init(w, h, NULL, sz, flags);
}

/***************************************************************************
 * GBC::GRAPHICBUFFERCLASS -- Default constructor                          *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicBufferClass::GraphicBufferClass(void)
{
	// Initialize to empty state
	Width = 0;
	Height = 0;
	Buffer = NULL;
	Size = 0;
	Allocated = FALSE;
	Offset = 0;
	IsDirectDraw = FALSE;
	Pitch = 0;
	XAdd = 0;
	XPos = 0;
	YPos = 0;
	GraphicBuff = this;
	LockCount = 0;
	SurfaceFormat = 0;
	VideoSurfacePtr = NULL;
}

/***************************************************************************
 * GBC::UN_INIT -- releases the video surface belonging to this gbuffer    *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void GraphicBufferClass::Un_Init(void)
{
	// For Atari ST, just free the buffer if it was allocated
	if (Allocated && Buffer) {
		delete[] (unsigned char *)Buffer;
		Buffer = NULL;
		Allocated = FALSE;
	}
}

/***************************************************************************
 * GBC::~GRAPHICBUFFERCLASS -- Destructor for GraphicBufferClass          *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.cpp                                     *
 *=========================================================================*/
GraphicBufferClass::~GraphicBufferClass(void)
{
	Un_Init();
}

/***************************************************************************
 * GVPC::PUT_PIXEL -- Puts a pixel on a graphic viewport                   *
 *                                                                         *
 * INPUT:   x, y - pixel coordinates                                       *
 *          color - color value                                            *
 *                                                                         *
 * OUTPUT:  none                                                           *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h (inline implementation)               *
 *=========================================================================*/
void GraphicViewPortClass::Put_Pixel(int x, int y, unsigned char color)
{
	if (Lock()){
		Buffer_Put_Pixel(this, x, y, color);
	}
	Unlock();
}

/***************************************************************************
 * GVPC::GET_PIXEL -- Gets a pixel from a graphic viewport                 *
 *                                                                         *
 * INPUT:   x, y - pixel coordinates                                       *
 *                                                                         *
 * OUTPUT:  int - pixel color value                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/gbuffer.h                                       *
 *=========================================================================*/
int GraphicViewPortClass::Get_Pixel(int x, int y)
{
	int return_code = 0;
	if (Lock()){
		return_code = Buffer_Get_Pixel(this, x, y);
	}
	Unlock();
	return return_code;
}

