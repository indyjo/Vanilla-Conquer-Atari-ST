/*
 * mouseww.cpp - Mouse input implementation for Atari ST/MiNT
 * 
 * Provides implementations of mouse functions needed by main source code.
 */

#include "mouse.h"
#include "keyboard.h"
#include "ikbd.h"
#include "gbuffer.h"
#include "drawbuff.h"  // Buffer_To_Page, Buffer_From_Page
#include "c2p.h"       // ST_PLANAR_BYTES_PER_LINE
#include "shape.h"     // Get_Shape_Width, Get_Shape_Height, Decode_Shape_To_Buffer
#include <mint/linea.h>  // GCURX, GCURY, MOUSE_BT (LINE-A)
#include <mint/ostruct.h> // BLIT_HARD
#include <mint/osbind.h>  // Blitmode
#include <stdint.h>
#include <string.h>     // memset

static int Mouse_Planar_Row_Bytes(GraphicBufferClass *gb)
{
	if (!gb || !gb->Is_ST_Planar())
		return ST_PLANAR_BYTES_PER_LINE;
	int const p = gb->Get_Pitch();
	return (p > 0) ? p : ST_Planar_Row_Bytes(gb->Get_Width());
}

// Global mouse object pointer
void* _Mouse = NULL;

// DLL force mouse position (used when mouse is controlled externally)
int DLLForceMouseX = -1;
int DLLForceMouseY = -1;

static inline volatile unsigned short *ST_BLT_REG(unsigned long addr)
{
	return (volatile unsigned short *)addr;
}

static void ST_Blit_Wait_Idle(void)
{
	volatile unsigned char *ctrl = (volatile unsigned char *)0xFFFF8A3CL;
	while ((*ctrl & 0x80u) != 0) {
	}
}

static void ST_Copy_Bytes_2D(const uint8_t *src, uint8_t *dst, int row_bytes, int lines, int src_stride, int dst_stride)
{
	if (!src || !dst || row_bytes <= 0 || lines <= 0)
		return;
	for (int y = 0; y < lines; y++) {
		memcpy(dst, src, (size_t)row_bytes);
		src += src_stride;
		dst += dst_stride;
	}
}

/*
 * One ST low-res bitplane: horizontal skew from interleaved src to interleaved dst.
 * src_row_bytes / dst_row_bytes: full planar row strides (multiple of 8).
 * dst_words: destination 16-pixel columns (same as mouse "words" in Draw_Mouse).
 * plane_idx: 0..3 (word at +plane*2 within each 16-pixel group).
 * skew_bits: 0..15 pixel shift right of source into destination (ST $FF8A3D).
 */
static void ST_Blit_Plane_Skew(
	const uint8_t *src_base, uint8_t *dst_base,
	int src_row_bytes, int dst_row_bytes,
	int dst_words, int lines, int plane_idx, unsigned skew_bits, unsigned char op,
	unsigned short endmask1, unsigned short endmask2, unsigned short endmask3)
{
	if (!src_base || !dst_base || dst_words <= 0 || lines <= 0 || plane_idx < 0 || plane_idx > 3)
		return;
	const int sx = 8;
	const int dx = 8;
	const int src_y_inc = src_row_bytes - (dst_words - 1) * sx;
	const int dst_y_inc = dst_row_bytes - (dst_words - 1) * dx;
	ST_Blit_Wait_Idle();
	*ST_BLT_REG(0xFFFF8A20UL) = (unsigned short)sx;
	*ST_BLT_REG(0xFFFF8A22UL) = (unsigned short)src_y_inc;
	{
		size_t sa = (size_t)(src_base + plane_idx * 2);
		*ST_BLT_REG(0xFFFF8A24UL) = (unsigned short)(sa >> 16);
		*ST_BLT_REG(0xFFFF8A26UL) = (unsigned short)(sa & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A28UL) = endmask1;
	*ST_BLT_REG(0xFFFF8A2AUL) = endmask2;
	*ST_BLT_REG(0xFFFF8A2CUL) = endmask3;
	*ST_BLT_REG(0xFFFF8A2EUL) = (unsigned short)dx;
	*ST_BLT_REG(0xFFFF8A30UL) = (unsigned short)dst_y_inc;
	{
		size_t da = (size_t)(dst_base + plane_idx * 2);
		*ST_BLT_REG(0xFFFF8A32UL) = (unsigned short)(da >> 16);
		*ST_BLT_REG(0xFFFF8A34UL) = (unsigned short)(da & 0xFFFFu);
	}
	*ST_BLT_REG(0xFFFF8A36UL) = (unsigned short)dst_words;
	*ST_BLT_REG(0xFFFF8A38UL) = (unsigned short)lines;
	*(volatile unsigned char *)0xFFFF8A3AUL = 2;              /* HOP: source */
	*(volatile unsigned char *)0xFFFF8A3BUL = op;             /* OP */
	{
		/* Keep to pure 4-bit skew value first; extra control bits are blitter-revision sensitive. */
		unsigned char skewb = (unsigned char)(skew_bits & 15u);
		*(volatile unsigned char *)0xFFFF8A3DUL = skewb;
	}
	*(volatile unsigned char *)0xFFFF8A3CUL = 0x80;           /* start */
	ST_Blit_Wait_Idle();
}

static void ST_Blit_Interleaved_Block_Skew(
	const uint8_t *src, uint8_t *dst,
	int src_row_bytes, int dst_row_bytes,
	int dst_words, int lines, unsigned skew_bits, unsigned char op,
	unsigned short endmask1, unsigned short endmask2, unsigned short endmask3)
{
	for (int pl = 0; pl < 4; pl++)
		ST_Blit_Plane_Skew(src, dst, src_row_bytes, dst_row_bytes, dst_words, lines, pl, skew_bits, op,
			endmask1, endmask2, endmask3);
}

/***********************************************************************************************
 * WWMouseClass::WWMouseClass -- Constructor for the Mouse Class                               *
 *                                                                                             *
 * INPUT:		GraphicViewPortClass * screen - pointer to screen mouse is created for         *
 *					int mouse_max_width - maximum width of mouse cursor                         *
 *					int mouse_max_height - maximum height of mouse cursor                       *
 *                                                                                             *
 * OUTPUT:     none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/12/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
WWMouseClass::WWMouseClass(GraphicViewPortClass *scr, int mouse_max_width, int mouse_max_height)
{
	MouseCursor 	= new char[mouse_max_width * mouse_max_height];
	MouseXHot		= 0;
	MouseYHot		= 0;
	CursorWidth		= 0;
	CursorHeight	= 0;

	const int blit_max_width = mouse_max_width + 31; /* word-align headroom */
	const int blit_max_words = (blit_max_width + 15) >> 4;
	const int blit_max_bytes = blit_max_words * 8 * mouse_max_height;
	MouseBlitRowBytes = blit_max_words * 8;
	MouseBuffer		= new char[blit_max_bytes];
	MousePlanarColorPre = new unsigned char[blit_max_bytes];
	MouseBuffX		= -1;
	MouseBuffY  	= -1;
	MouseBuffLeft	= -1;
	MouseBuffTop	= -1;
	MouseBuffWords	= 0;
	MouseBuffH		= 0;
	MousePosX		= -1;
	MousePosY		= -1;
	MaxWidth			= mouse_max_width;
	MaxHeight		= mouse_max_height;

	MouseCXLeft		= 0;
	MouseCYUpper	= 0;
	MouseCXRight	= 0;
	MouseCYLower	= 0;
	MCFlags			= 0;
	MCCount			= 0;

	Screen			= scr;
	PrevCursor		= NULL;
	MouseUpdate		= 0;
	State				= 1;  // Start hidden (Windows compatibility: State=0 is visible, State>0 is hidden)

	EraseBuffer		= new char[blit_max_bytes];
	EraseBuffX		= -1;
	EraseBuffY  	= -1;
	EraseBuffHotX	= -1;
	EraseBuffHotY	= -1;
	EraseBuffLeft	= -1;
	EraseBuffTop	= -1;
	EraseBuffWords	= 0;
	EraseBuffH		= 0;
	EraseFlags		= FALSE;

	LastMouseBt		= -1;

	_Mouse			= this;
	
	/*
	** Force the mouse pointer to stay within the graphic view port region
	*/
	Set_Cursor_Clip();
}

WWMouseClass::~WWMouseClass()
{
	MouseUpdate++;

	if (_Mouse == this) {
		_Mouse = NULL;
	}

	if (MouseCursor) delete[] MouseCursor;
	if (MouseBuffer) delete[] MouseBuffer;
	if (MousePlanarColorPre) delete[] MousePlanarColorPre;
	if (EraseBuffer) delete[] EraseBuffer;

	/*
	** Free up the mouse pointer movement
	*/
	Clear_Cursor_Clip();
}

/***************************************************************************
 * WWMouseClass::Set_Cursor_Clip -- Clips mouse cursor to viewport         *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST - no clipping needed                                *
 *=========================================================================*/
void WWMouseClass::Set_Cursor_Clip(void)
{
	// Stub for Atari ST - no Windows-style cursor clipping needed
}

/***************************************************************************
 * WWMouseClass::Clear_Cursor_Clip -- Removes cursor clipping             *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST - no clipping needed                                *
 *=========================================================================*/
void WWMouseClass::Clear_Cursor_Clip(void)
{
	// Stub for Atari ST - no Windows-style cursor clipping needed
}

/***************************************************************************
 * WWMouseClass::Set_Cursor -- Sets the mouse cursor                       *
 *                                                                         *
 * INPUT:		int xhotspot, yhotspot - hotspot coordinates                *
 *					void *cursor - cursor data                               *
 *                                                                         *
 * OUTPUT:     void* - previous cursor                                     *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void *WWMouseClass::Set_Cursor(int xhotspot, int yhotspot, void *cursor)
{
	void *old_cursor = PrevCursor;
	const int was_visible = (State == 0 && Screen);
	const int show_x = was_visible ? Get_Mouse_X() : 0;
	const int show_y = was_visible ? Get_Mouse_Y() : 0;

	MouseUpdate++;
	if (was_visible && Screen->Lock()) {
		Low_Hide_Mouse();
		Screen->Unlock();
	}

	PrevCursor = (char *)cursor;
	MouseXHot = xhotspot;
	MouseYHot = yhotspot;
	if (cursor) {
		CursorWidth = Get_Shape_Width(cursor);
		CursorHeight = Get_Shape_Height(cursor);
		if (CursorWidth > MaxWidth) CursorWidth = MaxWidth;
		if (CursorHeight > MaxHeight) CursorHeight = MaxHeight;
		if (CursorWidth > 0 && CursorHeight > 0) {
			int decoded = Decode_Shape_To_Buffer(cursor, MouseCursor, MaxWidth * MaxHeight);
			if (decoded == 0)
				memset(MouseCursor, 0, (unsigned)(CursorWidth * CursorHeight));
		}
		/* Invalidate saved position so next Draw_Mouse doesn't restore garbage */
		MouseBuffX = -1;
		MouseBuffY = -1;
		MouseBuffLeft = -1;
		MouseBuffTop = -1;
		MouseBuffWords = 0;
		MouseBuffH = 0;
		Rebuild_Planar_Cursor_From_Decoded();
	} else {
		CursorWidth = 0;
		CursorHeight = 0;
	}
	if (was_visible && Screen->Lock()) {
		Low_Show_Mouse(show_x, show_y);
		Screen->Unlock();
	}
	MouseUpdate--;
	return old_cursor;
}

/***************************************************************************
 * WWMouseClass::Set_Cursor_From_Block -- Set cursor from TD SHP block     *
 *                                                                         *
 * Use for MOUSE.SHP (Tiberian Dawn format). Decodes frame_index with LCW. *
 *=========================================================================*/
void WWMouseClass::Set_Cursor_From_Block(int hotx, int hoty, void *block, int frame_index)
{
	const int was_visible = (State == 0 && Screen);
	const int show_x = was_visible ? Get_Mouse_X() : 0;
	const int show_y = was_visible ? Get_Mouse_Y() : 0;

	MouseUpdate++;
	if (was_visible && Screen->Lock()) {
		Low_Hide_Mouse();
		Screen->Unlock();
	}

	MouseXHot = hotx;
	MouseYHot = hoty;
	PrevCursor = (char *)block;  /* non-null so Draw_Mouse runs */
	if (!block) {
		CursorWidth = 0;
		CursorHeight = 0;
		MouseBuffX = -1;
		MouseBuffY = -1;
		if (was_visible && Screen->Lock()) {
			Low_Show_Mouse(show_x, show_y);
			Screen->Unlock();
		}
		MouseUpdate--;
		return;
	}
	CursorWidth = Get_TD_SHP_Width(block);
	CursorHeight = Get_TD_SHP_Height(block);
	if (CursorWidth > MaxWidth) CursorWidth = MaxWidth;
	if (CursorHeight > MaxHeight) CursorHeight = MaxHeight;
	if (CursorWidth > 0 && CursorHeight > 0) {
		int decoded = Decode_TD_SHP_Frame(block, frame_index, MouseCursor, MaxWidth * MaxHeight);
		if (decoded == 0)
			memset(MouseCursor, 0, (unsigned)(CursorWidth * CursorHeight));
	}
	MouseBuffX = -1;
	MouseBuffY = -1;
	MouseBuffLeft = -1;
	MouseBuffTop = -1;
	MouseBuffWords = 0;
	MouseBuffH = 0;
	Rebuild_Planar_Cursor_From_Decoded();
	if (was_visible && Screen->Lock()) {
		Low_Show_Mouse(show_x, show_y);
		Screen->Unlock();
	}
	MouseUpdate--;
}

/***************************************************************************
 * WWMouseClass::Rebuild_Planar_Cursor_From_Decoded -- planar color         *
 *                                                                         *
 * Builds canonical (X shift 0) MousePlanarColorPre.
 * Draw_Mouse skew-copies it per pixel offset via the blitter and ORs it
 * directly into VRAM (transparent pixels are color 0).
 *=========================================================================*/
void WWMouseClass::Rebuild_Planar_Cursor_From_Decoded(void)
{
	if (CursorWidth <= 0 || CursorHeight <= 0 || !MousePlanarColorPre)
		return;
	const int blit_bytes = MouseBlitRowBytes * MaxHeight;
	memset(MousePlanarColorPre, 0, (size_t)blit_bytes);
	const unsigned char *cur = (const unsigned char *)MouseCursor;
	for (int row = 0; row < CursorHeight; row++) {
		unsigned char *color = MousePlanarColorPre + row * MouseBlitRowBytes;
		for (int col = 0; col < CursorWidth; col++) {
			const unsigned char px = cur[col];
			if (px == 0)
				continue;
			const int ax = col;
			const int group = ax >> 4;
			const int half = (ax >> 3) & 1;
			unsigned char *c = color + group * 8 + half;
			const int bitnum = 7 - (ax & 7);
			const unsigned char bit = (unsigned char)(1u << bitnum);
			for (int pl = 0; pl < 4; pl++) {
				unsigned char *cb = c + pl * 2;
				if (px & (1u << pl))
					*cb |= bit;
			}
		}
		cur += CursorWidth;
	}
}

/***************************************************************************
 * WWMouseClass::Show_Mouse -- Shows the mouse cursor                      *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void WWMouseClass::Show_Mouse(void)
{
	MouseUpdate++;
	if (State > 0) {
		State--;
		if (State == 0 && Screen) {
			int x = Get_Mouse_X();
			int y = Get_Mouse_Y();
			if (Screen->Lock()) {
				Low_Show_Mouse(x, y);
				Screen->Unlock();
			}
		}
	}
	MouseUpdate--;
}

/***************************************************************************
 * WWMouseClass::Hide_Mouse -- Hides the mouse cursor                      *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void WWMouseClass::Hide_Mouse(void)
{
	MouseUpdate++;
	if (State == 0 && Screen) {
		if (Screen->Lock()) {
			Low_Hide_Mouse();
			Screen->Unlock();
		}
	}
	State++;
	MouseUpdate--;
}

void WWMouseClass::Low_Hide_Mouse(void)
{
	if (!Screen)
		return;
	GraphicBufferClass *gb = Screen->Get_Graphic_Buffer();
	if (!gb || !gb->Uses_ST_LoRes_Planar_Layout())
		return;
	const int scr_bpl = Mouse_Planar_Row_Bytes(gb);
	if (MouseBuffWords > 0 && MouseBuffH > 0 && MouseBuffLeft >= 0 && MouseBuffTop >= 0) {
		uint8_t *dst = (uint8_t *)gb->Get_Buffer()
			+ MouseBuffTop * scr_bpl
			+ ((MouseBuffLeft >> 4) * 8);
		const int row_bytes = MouseBuffWords * 8;
		ST_Copy_Bytes_2D((const uint8_t *)MouseBuffer, dst, row_bytes, MouseBuffH, row_bytes, scr_bpl);
	}
	MouseBuffX = -1;
	MouseBuffY = -1;
	MouseBuffLeft = -1;
	MouseBuffTop = -1;
	MouseBuffWords = 0;
	MouseBuffH = 0;
}

void WWMouseClass::Low_Show_Mouse(int x, int y)
{
	if (!Screen || State != 0 || !PrevCursor || CursorWidth <= 0 || CursorHeight <= 0)
		return;
	GraphicBufferClass *gb = Screen->Get_Graphic_Buffer();
	if (!gb || !gb->Uses_ST_LoRes_Planar_Layout())
		return;
	const int scr_bpl = Mouse_Planar_Row_Bytes(gb);

	int vpw = Screen->Get_Width();
	int vph = Screen->Get_Height();
	int left = x - MouseXHot;
	int top = y - MouseYHot;
	int clip_left = left < 0 ? 0 : left;
	int clip_top = top < 0 ? 0 : top;
	int clip_right = left + CursorWidth;
	int clip_bottom = top + CursorHeight;
	if (clip_right > vpw) clip_right = vpw;
	if (clip_bottom > vph) clip_bottom = vph;
	const int vis_w = clip_right - clip_left;
	const int vis_h = clip_bottom - clip_top;

	MouseBuffX = x;
	MouseBuffY = y;
	if (vis_w <= 0 || vis_h <= 0) {
		MouseBuffLeft = -1;
		MouseBuffTop = -1;
		MouseBuffWords = 0;
		MouseBuffH = 0;
		return;
	}

	const int word_left = clip_left & ~15;
	const int word_right = (clip_right + 15) & ~15;
	const int words = (word_right - word_left) >> 4;
	const int row_bytes = words * 8;
	uint8_t *src_bg = (uint8_t *)gb->Get_Buffer()
		+ clip_top * scr_bpl
		+ ((word_left >> 4) * 8);
	ST_Copy_Bytes_2D(src_bg, (uint8_t *)MouseBuffer, row_bytes, vis_h, scr_bpl, row_bytes);

	MouseBuffLeft = word_left;
	MouseBuffTop = clip_top;
	MouseBuffWords = words;
	MouseBuffH = vis_h;

	Draw_Mouse(Screen);
}

/***************************************************************************
 * WWMouseClass::Conditional_Hide_Mouse -- Conditionally hides mouse      *
 *                                                                         *
 * INPUT:		int x1, y1, x2, y2 - region coordinates                    *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void WWMouseClass::Conditional_Hide_Mouse(int x1, int y1, int x2, int y2)
{
	MouseUpdate++;

	if (Screen && CursorWidth > 0 && CursorHeight > 0) {
		x1 -= (CursorWidth - MouseXHot);
		if (x1 < 0) x1 = 0;
		y1 -= (CursorHeight - MouseYHot);
		if (y1 < 0) y1 = 0;
		x2 += MouseXHot;
		if (x2 > Screen->Get_Width()) x2 = Screen->Get_Width();
		y2 += MouseYHot;
		if (y2 > Screen->Get_Height()) y2 = Screen->Get_Height();
	}
	
	if (!MCCount) {
		MouseCXLeft		= x1;
		MouseCYUpper	= y1;
		MouseCXRight	= x2;
		MouseCYLower	= y2;
	} else {
		// Expand region
		if (x1 < MouseCXLeft) MouseCXLeft = x1;
		if (y1 < MouseCYUpper) MouseCYUpper = y1;
		if (x2 > MouseCXRight) MouseCXRight = x2;
		if (y2 > MouseCYLower) MouseCYLower = y2;
	}

	if (!(MCFlags & CONDHIDDEN) && Screen && State == 0) {
		const int mouse_x = Get_Mouse_X();
		const int mouse_y = Get_Mouse_Y();
		if (mouse_x >= MouseCXLeft && mouse_x <= MouseCXRight
			&& mouse_y >= MouseCYUpper && mouse_y <= MouseCYLower) {
			if (Screen->Lock()) {
				Low_Hide_Mouse();
				MCFlags |= CONDHIDDEN;
				Screen->Unlock();
			}
		}
	}
	
	MCFlags |= CONDHIDE;
	MCCount++;
	MouseUpdate--;
}

/***************************************************************************
 * WWMouseClass::Conditional_Show_Mouse -- Conditionally shows mouse        *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void WWMouseClass::Conditional_Show_Mouse(void)
{
	MouseUpdate++;
	
	if (MCCount > 0) {
		MCCount--;
		if (MCCount == 0) {
			if ((MCFlags & CONDHIDDEN) && Screen && State == 0) {
				const int mouse_x = Get_Mouse_X();
				const int mouse_y = Get_Mouse_Y();
				if (Screen->Lock()) {
					Low_Show_Mouse(mouse_x, mouse_y);
					Screen->Unlock();
				}
			}
			MCFlags = 0;
		}
	}
	
	MouseUpdate--;
}

/***************************************************************************
 * WWMouseClass::Get_Mouse_State -- Gets the mouse state                   *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - mouse state (1 = visible, 0 = hidden)                *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
int WWMouseClass::Get_Mouse_State(void)
{
	return State;
}

/***************************************************************************
 * WWMouseClass::Get_Mouse_X -- Gets the mouse X position                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - mouse X position                                      *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
int WWMouseClass::Get_Mouse_X(void)
{
	if (DLLForceMouseX >= 0) {
		return DLLForceMouseX;
	}
	return MousePosX;
}

/***************************************************************************
 * WWMouseClass::Get_Mouse_Y -- Gets the mouse Y position                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - mouse Y position                                      *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
int WWMouseClass::Get_Mouse_Y(void)
{
	if (DLLForceMouseY >= 0) {
		return DLLForceMouseY;
	}
	return MousePosY;
}

/***************************************************************************
 * WWMouseClass::Process_Mouse -- Processes mouse updates                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp (simplified for Atari ST)          *
 *=========================================================================*/
void WWMouseClass::Process_Mouse(void)
{
	int mouse_x = 0;
	int mouse_y = 0;

	if (DLLForceMouseX >= 0 && DLLForceMouseY >= 0) {
		mouse_x = DLLForceMouseX;
		mouse_y = DLLForceMouseY;
	} else {
		IKBD_Get_Mouse_XY(&mouse_x, &mouse_y);
	}
	
	// Clamp to screen bounds if Screen is set
	if (Screen) {
		int max_x = Screen->Get_Width() - 1;
		int max_y = Screen->Get_Height() - 1;
		
		if (mouse_x < 0) mouse_x = 0;
		if (mouse_x > max_x) mouse_x = max_x;
		if (mouse_y < 0) mouse_y = 0;
		if (mouse_y > max_y) mouse_y = max_y;
	}
	
	MousePosX = mouse_x;
	MousePosY = mouse_y;

	/* Queue-compatible mouse position comes from IKBD packet decoding. */
	if (_Kbd) {
		IKBD_Get_Mouse_XY(&_Kbd->MouseQX, &_Kbd->MouseQY);
	}

	/*
	 * Win32-like behavior: poll position and redraw cursor only when it moved.
	 * This keeps mouse painting event-driven by movement instead of per-frame loops.
	 */
	if (Screen && State == 0 && !MouseUpdate) {
		if (EraseFlags)
			return;
		if (mouse_x != MouseBuffX || mouse_y != MouseBuffY) {
			if (Screen->Lock()) {
				Low_Hide_Mouse();
				if (MCFlags & CONDHIDE
					&& mouse_x >= MouseCXLeft && mouse_x <= MouseCXRight
					&& mouse_y >= MouseCYUpper && mouse_y <= MouseCYLower) {
					MCFlags |= CONDHIDDEN;
				} else {
					MCFlags &= ~CONDHIDDEN;
				}
				if (!(MCFlags & CONDHIDDEN)) {
					Low_Show_Mouse(mouse_x, mouse_y);
				}
				Screen->Unlock();
			}
		}
	}
}

// Get mouse X position
int Get_Mouse_X(void)
{
	if (DLLForceMouseX >= 0) {
		return DLLForceMouseX;
	}
	if (!_Mouse) {
		int x = 0;
		IKBD_Get_Mouse_XY(&x, NULL);
		return x;
	}
	((WWMouseClass *)_Mouse)->Process_Mouse();
	return ((WWMouseClass *)_Mouse)->Get_Mouse_X();
}

// Get mouse Y position
int Get_Mouse_Y(void)
{
	if (DLLForceMouseY >= 0) {
		return DLLForceMouseY;
	}
	if (!_Mouse) {
		int y = 0;
		IKBD_Get_Mouse_XY(NULL, &y);
		return y;
	}
	((WWMouseClass *)_Mouse)->Process_Mouse();
	return ((WWMouseClass *)_Mouse)->Get_Mouse_Y();
}

/***************************************************************************
 * Show_Mouse -- Global function to show the mouse cursor                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
void Show_Mouse(void)
{
	if (_Mouse) {
		((WWMouseClass *)_Mouse)->Show_Mouse();
	}
}

/***************************************************************************
 * Hide_Mouse -- Global function to hide the mouse cursor                  *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
void Hide_Mouse(void)
{
	if (_Mouse) {
		((WWMouseClass *)_Mouse)->Hide_Mouse();
	}
}

/***************************************************************************
 * Conditional_Hide_Mouse -- Global function to conditionally hide mouse   *
 *                                                                         *
 * INPUT:		int x1, y1, x2, y2 - region coordinates                    *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
void Conditional_Hide_Mouse(int x1, int y1, int x2, int y2)
{
	if (_Mouse) {
		((WWMouseClass *)_Mouse)->Conditional_Hide_Mouse(x1, y1, x2, y2);
	}
}

/***************************************************************************
 * Conditional_Show_Mouse -- Global function to conditionally show mouse   *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
void Conditional_Show_Mouse(void)
{
	if (_Mouse) {
		((WWMouseClass *)_Mouse)->Conditional_Show_Mouse();
	}
}

/***************************************************************************
 * Get_Mouse_State -- Global function to get mouse state                   *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     int - mouse state                                           *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
int Get_Mouse_State(void)
{
	if (_Mouse) {
		return ((WWMouseClass *)_Mouse)->Get_Mouse_State();
	}
	return 0;
}

/***************************************************************************
 * Set_Mouse_Cursor -- Global function to set mouse cursor                 *
 *                                                                         *
 * INPUT:		int hotx, hoty - hotspot coordinates                        *
 *					void *cursor - cursor data                               *
 *                                                                         *
 * OUTPUT:     void* - previous cursor                                     *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/mouseww.cpp                                     *
 *=========================================================================*/
void *Set_Mouse_Cursor(int hotx, int hoty, void *cursor)
{
	if (_Mouse) {
		return ((WWMouseClass *)_Mouse)->Set_Cursor(hotx, hoty, cursor);
	}
	return NULL;
}

/***************************************************************************
 * Set_Mouse_Cursor_From_Block -- Set cursor from Tiberian Dawn SHP block   *
 *=========================================================================*/
void Set_Mouse_Cursor_From_Block(int hotx, int hoty, void *block, int frame_index)
{
	if (_Mouse) {
		/* MOUSE.SHP is a classic ShapeBlock: extract shape pointer then set cursor. */
		void *shape = Extract_Shape(block, frame_index);
		((WWMouseClass *)_Mouse)->Set_Cursor(hotx, hoty, shape);
	}
}

/***************************************************************************
 * WWMouseClass::Draw_Mouse -- Draws the mouse cursor on a viewport       *
 *                                                                         *
 * INPUT:		GraphicViewPortClass *scr - viewport to draw on            *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Internal utility: blit cursor shape only (no save/restore).          *
 *=========================================================================*/
void WWMouseClass::Draw_Mouse(GraphicViewPortClass *scr)
{
	if (!scr || State != 0 || !PrevCursor || CursorWidth <= 0 || CursorHeight <= 0)
		return;
	GraphicBufferClass *gb = scr->Get_Graphic_Buffer();
	if (!gb || !gb->Uses_ST_LoRes_Planar_Layout()) {
		return;
	}
	const int scr_bpl = Mouse_Planar_Row_Bytes(gb);
	int vpw = scr->Get_Width();
	int vph = scr->Get_Height();
	int x = Get_Mouse_X();
	int y = Get_Mouse_Y();
	int left = x - MouseXHot;
	int top = y - MouseYHot;
	int right = left + CursorWidth;
	int bottom = top + CursorHeight;
	const int using_external_surface = (scr != Screen);

	if (using_external_surface)
		EraseFlags = TRUE;

	if (right <= 0 || bottom <= 0 || left >= vpw || top >= vph) {
		if (using_external_surface)
			EraseFlags = FALSE;
		EraseBuffX = -1;
		EraseBuffY = -1;
		EraseBuffLeft = -1;
		EraseBuffTop = -1;
		EraseBuffWords = 0;
		EraseBuffH = 0;
		return;
	}

	if (using_external_surface) {
		EraseBuffX = x;
		EraseBuffY = y;
		EraseBuffHotX = MouseXHot;
		EraseBuffHotY = MouseYHot;
	} else {
		EraseBuffX = -1;
		EraseBuffY = -1;
		EraseBuffLeft = -1;
		EraseBuffTop = -1;
		EraseBuffWords = 0;
		EraseBuffH = 0;
	}

	if (left < 0 || top < 0 || right > vpw || bottom > vph) {
		const int clip_left = left < 0 ? 0 : left;
		const int clip_top = top < 0 ? 0 : top;
		const int clip_right = right > vpw ? vpw : right;
		const int clip_bottom = bottom > vph ? vph : bottom;
		const int src_x = clip_left - left;
		const int src_y = clip_top - top;
		const int vis_w = clip_right - clip_left;
		const int vis_h = clip_bottom - clip_top;
		if (vis_w <= 0 || vis_h <= 0) {
			if (using_external_surface)
				EraseFlags = FALSE;
			EraseBuffX = -1;
			EraseBuffY = -1;
			EraseBuffLeft = -1;
			EraseBuffTop = -1;
			EraseBuffWords = 0;
			EraseBuffH = 0;
			return;
		}
		const int word_left = clip_left & ~15;
		const int word_right = (clip_right + 15) & ~15;
		const int words = (word_right - word_left) >> 4;
		const int row_bytes = words * 8;
		uint8_t *src_bg = (uint8_t *)gb->Get_Buffer()
			+ clip_top * scr_bpl
			+ ((word_left >> 4) * 8);
		if (using_external_surface) {
		ST_Copy_Bytes_2D(src_bg, (uint8_t *)EraseBuffer, row_bytes, vis_h,
			scr_bpl, row_bytes);
			EraseBuffLeft = word_left;
			EraseBuffTop = clip_top;
			EraseBuffWords = words;
			EraseBuffH = vis_h;
			ST_Copy_Bytes_2D(src_bg, (uint8_t *)MouseBuffer, row_bytes, vis_h,
				scr_bpl, row_bytes);
			MouseBuffX = x;
			MouseBuffY = y;
			MouseBuffLeft = word_left;
			MouseBuffTop = clip_top;
			MouseBuffWords = words;
			MouseBuffH = vis_h;
		}
		const unsigned char *src = (const unsigned char *)MouseCursor + src_y * CursorWidth + src_x;
		for (int row = 0; row < vis_h; row++) {
			const unsigned char *s = src + row * CursorWidth;
			for (int col = 0; col < vis_w; col++) {
				const unsigned char px = s[col];
				if (px != 0) {
					Buffer_Put_Pixel(scr, clip_left + col, clip_top + row, px);
				}
			}
		}
		return;
	}

	/*
	 * Word-aligned cursor block (all planar):
	 * - OR skewed canonical cursor color (shift=0 source) directly into VRAM
	 *   (transparent pixels are encoded as color 0)
	 */
	const int word_left = left & ~15;
	const int shift = left - word_left;
	const int words = (shift + CursorWidth + 15) >> 4;
	const unsigned skew_bits = (unsigned)(shift & 15);
	const int row_bytes = words * 8;
	uint8_t *src_bg = (uint8_t *)gb->Get_Buffer()
		+ top * scr_bpl
		+ ((word_left >> 4) * 8);
	if (using_external_surface) {
		ST_Copy_Bytes_2D(src_bg, (uint8_t *)EraseBuffer, row_bytes, CursorHeight,
			scr_bpl, row_bytes);
		EraseBuffLeft = word_left;
		EraseBuffTop = top;
		EraseBuffWords = words;
		EraseBuffH = CursorHeight;
		ST_Copy_Bytes_2D(src_bg, (uint8_t *)MouseBuffer, row_bytes, CursorHeight, scr_bpl, row_bytes);
		MouseBuffX = x;
		MouseBuffY = y;
		MouseBuffLeft = word_left;
		MouseBuffTop = top;
		MouseBuffWords = words;
		MouseBuffH = CursorHeight;
	}
	const int end = (left + CursorWidth - 1) & 15;
	unsigned short endmask1 = (unsigned short)(0xFFFFu >> shift);
	unsigned short endmask3 = (unsigned short)(0xFFFFu << (15 - end));
	unsigned short endmask2 = 0xFFFFu;
	if (words == 1) {
		endmask1 = (unsigned short)(endmask1 & endmask3);
		endmask2 = endmask1;
		endmask3 = endmask1;
	}
	uint8_t *dst = (uint8_t *)gb->Get_Buffer()
		+ top * scr_bpl
		+ ((word_left >> 4) * 8);
	ST_Blit_Interleaved_Block_Skew(
		(const uint8_t *)MousePlanarColorPre,
		dst,
		MouseBlitRowBytes,
		scr_bpl,
		words,
		CursorHeight,
		skew_bits,
		7 /* OP: D = D OR S */,
		endmask1,
		endmask2,
		endmask3);
}

/***************************************************************************
 * WWMouseClass::Erase_Mouse -- Erases the mouse cursor from a viewport   *
 *                                                                         *
 * INPUT:		GraphicViewPortClass *scr - viewport to erase from         *
 *					int forced - force erase flag                            *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Restore EraseBuffer to scr when forced or when cursor was drawn there.*
 *=========================================================================*/
void WWMouseClass::Erase_Mouse(GraphicViewPortClass *scr, int forced)
{
	if (!scr || (EraseBuffX < 0 && !forced))
		return;
	const int using_external_surface = (scr != Screen);
	if (EraseBuffWords > 0 && EraseBuffH > 0 && EraseBuffLeft >= 0 && EraseBuffTop >= 0) {
		if (scr->Lock()) {
			GraphicBufferClass *gb = scr->Get_Graphic_Buffer();
			if (gb && gb->Uses_ST_LoRes_Planar_Layout()) {
				const int scr_bpl = Mouse_Planar_Row_Bytes(gb);
				uint8_t *dst = (uint8_t *)gb->Get_Buffer()
					+ EraseBuffTop * scr_bpl
					+ ((EraseBuffLeft >> 4) * 8);
				const int row_bytes = EraseBuffWords * 8;
				ST_Copy_Bytes_2D((const uint8_t *)EraseBuffer, dst, row_bytes, EraseBuffH,
					row_bytes, scr_bpl);
			}
			scr->Unlock();
		}
		EraseBuffX = -1;
		EraseBuffY = -1;
		EraseBuffLeft = -1;
		EraseBuffTop = -1;
		EraseBuffWords = 0;
		EraseBuffH = 0;
	}
	if (using_external_surface)
		EraseFlags = FALSE;
	(void)forced;
}

