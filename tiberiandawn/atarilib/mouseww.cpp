/*
 * mouseww.cpp - Mouse input implementation for Atari ST/MiNT
 * 
 * Provides implementations of mouse functions needed by main source code.
 */

#include "mouse.h"
#include "gbuffer.h"

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
	State				= 1;

	EraseBuffer		= new char[mouse_max_width * mouse_max_height];
	EraseBuffX		= -1;
	EraseBuffY  	= -1;
	EraseBuffHotX	= -1;
	EraseBuffHotY	= -1;
	EraseFlags		= FALSE;

	_Mouse			= this;
	
	/*
	** Force the mouse pointer to stay within the graphic view port region
	*/
	Set_Cursor_Clip();
}

WWMouseClass::~WWMouseClass()
{
	MouseUpdate++;

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
	PrevCursor = cursor;
	
	if (cursor) {
		// TODO: Implement actual cursor setting for Atari ST
		MouseXHot = xhotspot;
		MouseYHot = yhotspot;
		// For now, just store the cursor pointer
		// In a real implementation, we would need to process the cursor data
		// and set up the cursor dimensions
	}
	
	return old_cursor;
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
	State = 1;  // Mark mouse as visible
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
	State = 0;  // Mark mouse as hidden
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
	// TODO: Implement actual mouse position tracking for Atari ST
	return MouseBuffX >= 0 ? MouseBuffX : 0;
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
	// TODO: Implement actual mouse position tracking for Atari ST
	return MouseBuffY >= 0 ? MouseBuffY : 0;
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
	// TODO: Implement actual mouse processing for Atari ST
	// This would typically update mouse position, handle clicks, etc.
}

// Stub: Get mouse X position
int Get_Mouse_X(void)
{
	// TODO: Implement actual mouse input for Atari ST
	// For now, return forced position or 0
	if (DLLForceMouseX >= 0) {
		return DLLForceMouseX;
	}
	if (!_Mouse) return 0;
	// TODO: Call actual mouse object method
	return 0;
}

// Stub: Get mouse Y position
int Get_Mouse_Y(void)
{
	// TODO: Implement actual mouse input for Atari ST
	// For now, return forced position or 0
	if (DLLForceMouseY >= 0) {
		return DLLForceMouseY;
	}
	if (!_Mouse) return 0;
	// TODO: Call actual mouse object method
	return 0;
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
 * WWMouseClass::Draw_Mouse -- Draws the mouse cursor on a viewport       *
 *                                                                         *
 * INPUT:		GraphicViewPortClass *scr - viewport to draw on            *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST - mouse drawing handled by system                  *
 *=========================================================================*/
void WWMouseClass::Draw_Mouse(GraphicViewPortClass *scr)
{
	// Stub for Atari ST - mouse drawing handled by system
	(void)scr;
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
 *   Stub for Atari ST - mouse erasing handled by system                  *
 *=========================================================================*/
void WWMouseClass::Erase_Mouse(GraphicViewPortClass *scr, int forced)
{
	// Stub for Atari ST - mouse erasing handled by system
	(void)scr;
	(void)forced;
}

