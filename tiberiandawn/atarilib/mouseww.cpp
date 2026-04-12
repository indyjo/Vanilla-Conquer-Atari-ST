/*
 * mouseww.cpp - Mouse input implementation for Atari ST/MiNT
 * 
 * Provides implementations of mouse functions needed by main source code.
 */

#include "mouse.h"
#include "keyboard.h"
#include "gbuffer.h"
#include "drawbuff.h"  // Buffer_To_Page, Buffer_From_Page
#include "shape.h"     // Get_Shape_Width, Get_Shape_Height, Decode_Shape_To_Buffer
#include <mint/linea.h>  // GCURX, GCURY, MOUSE_BT (LINE-A)
#include <string.h>     // memset

// Global mouse object pointer
void* _Mouse = NULL;

// DLL force mouse position (used when mouse is controlled externally)
int DLLForceMouseX = -1;
int DLLForceMouseY = -1;

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

	MouseBuffer		= new char[mouse_max_width * mouse_max_height];
	MouseBuffX		= -1;
	MouseBuffY  	= -1;
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

	EraseBuffer		= new char[mouse_max_width * mouse_max_height];
	EraseBuffX		= -1;
	EraseBuffY  	= -1;
	EraseBuffHotX	= -1;
	EraseBuffHotY	= -1;
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
	} else {
		CursorWidth = 0;
		CursorHeight = 0;
	}
	return old_cursor;
}

/***************************************************************************
 * WWMouseClass::Set_Cursor_From_Block -- Set cursor from TD SHP block     *
 *                                                                         *
 * Use for MOUSE.SHP (Tiberian Dawn format). Decodes frame_index with LCW. *
 *=========================================================================*/
void WWMouseClass::Set_Cursor_From_Block(int hotx, int hoty, void *block, int frame_index)
{
	MouseXHot = hotx;
	MouseYHot = hoty;
	PrevCursor = (char *)block;  /* non-null so Draw_Mouse runs */
	if (!block) {
		CursorWidth = 0;
		CursorHeight = 0;
		MouseBuffX = -1;
		MouseBuffY = -1;
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
	// Windows compatibility: State=0 is visible, State>0 is hidden
	// Decrement State to make mouse more visible (but don't go below 0)
	if (State > 0) {
		State--;
	}
	// TODO: Implement actual mouse showing for Atari ST
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
	// Windows compatibility: State=0 is visible, State>0 is hidden
	// Increment State to make mouse more hidden
	State++;
	// TODO: Implement actual mouse hiding for Atari ST
	MouseUpdate--;
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
			MCFlags &= ~CONDHIDE;
			MCFlags &= ~CONDHIDDEN;
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
	return MousePosX >= 0 ? MousePosX : GCURX;
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
	return MousePosY >= 0 ? MousePosY : GCURY;
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
	// Skip if forced position is set
	if (DLLForceMouseX >= 0 || DLLForceMouseY >= 0) {
		return;
	}
	
	// Read mouse position from LINE-A system variables (graphics cursor)
	int mouse_x = GCURX;
	int mouse_y = GCURY;
	
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

	/* Live position for UI that reads _Kbd->MouseQX/Y without dequeuing keys */
	if (_Kbd) {
		_Kbd->MouseQX = mouse_x;
		_Kbd->MouseQY = mouse_y;
	}

	/* Button edges from LINE-A MOUSE_BT (bit0=left, bit1=right), same as GCURX/GCURY */
	if (_Kbd) {
		int bt = (int)(MOUSE_BT & 3);
		if (LastMouseBt < 0) {
			LastMouseBt = bt;
		} else {
			int prev = LastMouseBt;
			if ((bt ^ prev) & 1) {
				_Kbd->Put_Key_Message(VK_LBUTTON, (bt & 1) == 0);
				_Kbd->Put(mouse_x);
				_Kbd->Put(mouse_y);
			}
			if ((bt ^ prev) & 2) {
				_Kbd->Put_Key_Message(VK_RBUTTON, (bt & 2) == 0);
				_Kbd->Put(mouse_x);
				_Kbd->Put(mouse_y);
			}
			LastMouseBt = bt;
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
		// Fallback to LINE-A if no mouse object
		return GCURX;
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
		// Fallback to LINE-A if no mouse object
		return GCURY;
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
 *   Analogous to WIN32: restore old background, save new, draw cursor.   *
 *=========================================================================*/
void WWMouseClass::Draw_Mouse(GraphicViewPortClass *scr)
{
	if (!scr || State != 0 || !PrevCursor || CursorWidth <= 0 || CursorHeight <= 0)
		return;
	if (!scr->Lock())
		return;
	/* Sample hardware; must not clobber MouseBuffX/Y (previous draw restore coords). */
	if (DLLForceMouseX < 0 && DLLForceMouseY < 0)
		Process_Mouse();
	int vpw = scr->Get_Width();
	int vph = scr->Get_Height();
	int x = Get_Mouse_X();
	int y = Get_Mouse_Y();
	/* Clamp so cursor rect stays fully inside viewport */
	if (x < MouseXHot) x = MouseXHot;
	if (y < MouseYHot) y = MouseYHot;
	if (x > vpw - CursorWidth + MouseXHot) x = vpw - CursorWidth + MouseXHot;
	if (y > vph - CursorHeight + MouseYHot) y = vph - CursorHeight + MouseYHot;
	int left = x - MouseXHot;
	int top = y - MouseYHot;
	if (left < 0 || top < 0) {
		scr->Unlock();
		return;
	}
	/* Restore background at previous cursor position */
	if (MouseBuffX >= 0 && MouseBuffY >= 0) {
		int old_left = MouseBuffX - MouseXHot;
		int old_top = MouseBuffY - MouseYHot;
		if (old_left >= 0 && old_top >= 0 &&
		    old_left + CursorWidth <= vpw && old_top + CursorHeight <= vph)
			Buffer_To_Page(old_left, old_top, CursorWidth, CursorHeight, MouseBuffer, scr);
	}
	/* Save background under new position */
	Buffer_From_Page(left, top, CursorWidth, CursorHeight, MouseBuffer, scr);
	/* Draw cursor (0 = transparent); use Buffer_Put_Pixel for linear + ST planar. */
	const unsigned char *cur = (const unsigned char *)MouseCursor;
	for (int row = 0; row < CursorHeight; row++) {
		for (int col = 0; col < CursorWidth; col++) {
			if (cur[col] != 0)
				Buffer_Put_Pixel(scr, left + col, top + row, cur[col]);
		}
		cur += CursorWidth;
	}
	MouseBuffX = x;
	MouseBuffY = y;
	scr->Unlock();
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
	if (EraseBuffX >= 0 && EraseBuffY >= 0 && CursorWidth > 0 && CursorHeight > 0) {
		if (scr->Lock()) {
			int left = EraseBuffX - EraseBuffHotX;
			int top = EraseBuffY - EraseBuffHotY;
			int vpw = scr->Get_Width();
			int vph = scr->Get_Height();
			if (left >= 0 && top >= 0 && left + CursorWidth <= vpw && top + CursorHeight <= vph)
				Buffer_To_Page(left, top, CursorWidth, CursorHeight, EraseBuffer, scr);
			scr->Unlock();
		}
		EraseBuffX = -1;
		EraseBuffY = -1;
	}
	(void)forced;
}

