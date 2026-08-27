/*
 * gbuffer.h - Graphic buffer header for Atari ST/MiNT
 * 
 * This provides a compatible interface to WIN32LIB/gbuffer.h
 */

#ifndef GBUFFER_H
#define GBUFFER_H

#include "windows.h"  // For BOOL type

#include "../COMMONLIB/wwstd.h"
#include "buffer.h" /* common/buffer.h (-I../common); GraphicBufferClass still owns the alloc */

// Forward declaration for Buffer_Fill_Quad
#ifdef __cplusplus
extern "C" {
#endif
VOID Buffer_Fill_Quad(void *thisptr, VOID *span_buff, int x0, int y0, int x1, int y1,
						int x2, int y2, int x3, int y3, int color);
#ifdef __cplusplus
}
#endif

// Type definitions for compatibility
typedef int HRESULT;
#ifndef VOID
#define VOID void
#endif

// Forward declarations
class GraphicViewPortClass;
class GraphicBufferClass;
class VideoViewPortClass;
class VideoBufferClass;

// Global LogicPage pointer
extern GraphicViewPortClass *LogicPage;

GraphicViewPortClass *Set_Logic_Page(GraphicViewPortClass *ptr);
GraphicViewPortClass *Set_Logic_Page(GraphicViewPortClass &ptr);

/*=========================================================================
 * GraphicViewPortClass - Holds viewport information on a viewport which
 *		has been attached to a GraphicBuffer.
 *=========================================================================*/
class GraphicViewPortClass {
	public:
		/*===================================================================*/
		/* Define the base constructor and destructors for the class		*/
		/*===================================================================*/
		GraphicViewPortClass(GraphicBufferClass* graphic_buff, int x, int y, int w, int h);
		GraphicViewPortClass();
		~GraphicViewPortClass();

		/*===================================================================*/
		/* define functions to get at the private data members				*/
		/*===================================================================*/
		long	Get_Offset(void);
		int	Get_Height(void);
		int	Get_Width(void);
		int	Get_XAdd(void);
		int	Get_XPos(void);
		int	Get_YPos(void);
		int	Get_Pitch(void);
		inline BOOL	Get_IsDirectDraw(void);
		GraphicBufferClass	*Get_Graphic_Buffer(void);

		/*===================================================================*/
		/* Define a function which allows us to change a video viewport on	*/
		/*		the fly.													*/
		/*===================================================================*/
		BOOL Change(int x, int y, int w, int h);

		/*===================================================================*/
		/* Define the set of common graphic functions that are supported by	*/
		/*		both Graphic ViewPorts and VideoViewPorts.					*/
		/*===================================================================*/
		long	Size_Of_Region(int w, int h);
		void	Put_Pixel(int x, int y, unsigned char color);
		int	Get_Pixel(int x, int y);
		void	Clear(unsigned char color = 0);
		long	To_Buffer(int x, int y, int w, int h, void *buff, long size);
		long	To_Buffer(int x, int y, int w, int h, BufferClass *buff);
		long	To_Buffer(BufferClass *buff);
		HRESULT	Blit(	GraphicViewPortClass& dest, int x_pixel, int y_pixel, int dx_pixel,
						int dy_pixel, int pixel_width, int pixel_height, BOOL trans = FALSE);
		HRESULT	Blit(	GraphicViewPortClass& dest, int dx, int dy, BOOL trans = FALSE);
		HRESULT	Blit(	GraphicViewPortClass& dest, BOOL trans = FALSE);
		HRESULT	Blit(	VideoViewPortClass& dest, int x_pixel, int y_pixel, int dx_pixel,
						int dy_pixel, int pixel_width, int pixel_height, BOOL trans = FALSE);
		HRESULT	Blit(	VideoViewPortClass& dest, int dx, int dy, BOOL trans = FALSE);
		HRESULT	Blit(	VideoViewPortClass& dest, BOOL trans = FALSE);
		BOOL	Scale(	GraphicViewPortClass &dest, int src_x, int src_y, int dst_x,
							int dst_y, int src_w, int src_h, int dst_w, int dst_h, BOOL trans = FALSE, char *remap = NULL);
		BOOL	Scale(	GraphicViewPortClass &dest, int src_x, int src_y, int dst_x,
							int dst_y, int src_w, int src_h, int dst_w, int dst_h, char *remap);
		BOOL	Scale(	GraphicViewPortClass &dest, BOOL trans = FALSE, char *remap = NULL);
		BOOL	Scale(	GraphicViewPortClass &dest, char *remap);
		BOOL	Scale(	VideoViewPortClass &dest, int src_x, int src_y, int dst_x,
							int dst_y, int src_w, int src_h, int dst_w, int dst_h, BOOL trans = FALSE, char *remap = NULL);
		BOOL	Scale(	VideoViewPortClass &dest, int src_x, int src_y, int dst_x,
							int dst_y, int src_w, int src_h, int dst_w, int dst_h, char *remap);
		BOOL	Scale(	VideoViewPortClass &dest, BOOL trans = FALSE, char *remap = NULL);
		BOOL	Scale(	VideoViewPortClass &dest, char *remap);
		unsigned long	Print(char const *string, int x_pixel, int y_pixel, int fcolor, int bcolor);
		unsigned long	Print(short num, int x_pixel, int y_pixel, int fcol, int bcol);
		unsigned long	Print(int num, int x_pixel, int y_pixel, int fcol, int bcol);
		unsigned long	Print(long num, int x_pixel, int y_pixel, int fcol, int bcol);

		/*===================================================================*/
		/* Define the list of graphic functions which work only with a 		*/
		/*		graphic buffer.												*/
		/*===================================================================*/
		VOID Draw_Line(int sx, int sy, int dx, int dy, unsigned char color);
		VOID Draw_Rect(int sx, int sy, int dx, int dy, unsigned char color);
		VOID Fill_Rect(int sx, int sy, int dx, int dy, unsigned char color);
		VOID Fill_Quad(VOID *span_buff, int x0, int y0, int x1, int y1,
							int x2, int y2, int x3, int y3, int color);
		VOID Remap(int sx, int sy, int width, int height, VOID *remap);
		VOID Remap(VOID *remap);
		void Draw_Stamp(void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap);
		void Draw_Stamp(void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int clip_window);

		void Texture_Fill_Rect (int xpos, int ypos, int width, int height, void const *shape_pointer, int source_width, int source_height) {
			return;
		}

		//
		// New members to lock and unlock the direct draw video memory
		//
		inline BOOL 	Lock ();
		inline BOOL 	Unlock();
		inline int		Get_LockCount();

		// Member to blit using direct draw access to hardware blitter
		HRESULT DD_Linear_Blit_To_Linear ( GraphicViewPortClass &dest, int source_x, int source_y, int dest_x, int dest_y, int width	, int height, BOOL mask );

		/*===================================================================*/
		/* Define functions to attach the viewport to a graphicbuffer		*/
		/*===================================================================*/
		VOID Attach(GraphicBufferClass *graphic_buff, int x, int y, int w, int h);
		void Attach(GraphicBufferClass *video_buff, int w, int h);

	protected:

		/*===================================================================*/
		/* Define the data used by a GraphicViewPortClass					*/
		/*===================================================================*/
		long					Offset;			// offset to graphic page
		int						Width;			// width of graphic page
		int						Height;			// height of graphic page
		int						XAdd;			// xadd for graphic page (0)
		int						XPos;			// x offset in relation to graphicbuff
		int						YPos;			// y offset in relation to graphicbuff
		long					Pitch;			//Distance from one line to the next
		GraphicBufferClass		*GraphicBuff;	// related graphic buff
		BOOL					IsDirectDraw;	//Flag to let us know if it is a direct draw surface
		int						LockCount;		// Count for stacking locks if non-zero the buffer
};                                              //   is a locked DD surface

/*=========================================================================
 * GraphicBufferClass - A GraphicBuffer refers to an actual instance of an
 *		allocated buffer.
 *=========================================================================*/
class GraphicBufferClass : public GraphicViewPortClass, public BufferClass {

	public:
		GraphicBufferClass(int w, int h, int flags);
		GraphicBufferClass(int w, int h,	void *buffer, long size);
		GraphicBufferClass(int w, int h, void *buffer = 0);
		GraphicBufferClass(void);
		~GraphicBufferClass();

		void Init(int w, int h, void *buffer, long size, int flags);
		void Un_Init(void);
		BOOL Lock(void);
		BOOL Unlock(void);

		/* ST LoRes planar: swap backing store with peer (same dimensions). */
		void Swap_Planar_Buffer_With(GraphicBufferClass &other);

		int Get_Surface_Format(void) const { return SurfaceFormat; }
		static BOOL Is_ST_Planar_Format(int fmt) { return fmt == 1; }
		BOOL Is_ST_Planar(void) const { return SurfaceFormat == 1; }
		/* True when SurfaceFormat is ST interleaved planar (any width/height; Pitch = bytes/line). */
		BOOL Uses_ST_LoRes_Planar_Layout(void) const { return Is_ST_Planar(); }

		/* Linear buffers only: set bytes of padding after each Width-wide row (row stride = Width + padding). */
		void Set_Linear_Row_Padding_Bytes(int padding_after_width);

		// Get_Buffer is inherited from BufferClass

	protected:
		void	*VideoSurfacePtr;		//Pointer to the related direct draw surface (stub)
		int		SurfaceFormat;			/* 0 = linear 8bpp, 1 = ST LoRes planar */

};

// Inline implementations
inline int GraphicViewPortClass::Get_LockCount(void)
{
	return (LockCount);
}

inline BOOL GraphicViewPortClass::Get_IsDirectDraw(void)
{
	return (IsDirectDraw);
}



inline BOOL GraphicViewPortClass::Lock(void)
{
	if (!GraphicBuff) return(FALSE);
	BOOL lock = GraphicBuff->Lock();
	if ( !lock ) return(FALSE);

	if (this != GraphicBuff) {
		Attach(GraphicBuff, XPos, YPos,  Width, Height);
	}
	return(TRUE);
}

inline BOOL GraphicViewPortClass::Unlock(void)
{
	if (!GraphicBuff) return(FALSE);
	BOOL unlock = GraphicBuff->Unlock();
	if (!unlock) return(FALSE);
	if (this != GraphicBuff && IsDirectDraw && !GraphicBuff->LockCount) {
		Offset = 0;
	}
	return(TRUE);
}

class VideoViewPortClass {
	// Stub - needs implementation
};

class VideoBufferClass {
	// Stub - needs implementation
};

/*=========================================================================*/
/* GBC_Enum - Graphic Buffer Class flags                                  */
/*=========================================================================*/
enum GBC_Enum {
	GBC_NONE				= 0,
	GBC_VIDEOMEM		= 1,
	GBC_VISIBLE			= 2,
	/* ST interleaved 16-color planar: Pitch = width/2 bytes per line; 320×200 → 32000 bytes. */
	GBC_ST_PLANAR_LORES	= 4,
};

/*=========================================================================*/
/* Define the screen width and height to make portability to other modules	*/
/*		easier.																					*/
/*=========================================================================*/
#define	DEFAULT_SCREEN_WIDTH		320
#define	DEFAULT_SCREEN_HEIGHT	200

#endif /* GBUFFER_H */
