/*
 * ww_win.h - Westwood window header stub for Atari ST/MiNT
 */

#ifndef WW_WIN_H
#define WW_WIN_H

/*
**	The WindowList[][8] array contains the following elements.  Use these
**	defines when accessing the WindowList.
*/
typedef enum {
	WINDOWX,			// X byte position of left edge.
	WINDOWY,			// Y pixel position of top edge.
	WINDOWWIDTH,	// Width in bytes of the window.
	WINDOWHEIGHT,	// Height in pixels of the window.
	WINDOWFCOL,		// Default foreground color.
	WINDOWBCOL,		// Default background color.
	WINDOWCURSORX,	// Current cursor X position (in rows).
	WINDOWCURSORY,	// Current cursor Y position (in lines).
	WINDOWPADDING=0x1000
} WindowIndexType;

extern int WindowList[][8];
extern int WindowColumns;
extern int WindowLines;

// Window mouse functions
void Window_Hide_Mouse(int window);
void Window_Show_Mouse(void);

#endif /* WW_WIN_H */

