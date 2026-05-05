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
	WINDOWX,			// X pixel position of left edge.
	WINDOWY,			// Y pixel position of top edge.
	WINDOWWIDTH,	// Width in pixels of the window.
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

// Window management functions
int Change_Window(int windnum);
int Change_New_Window(int windnum);
void New_Window(void);
void Window_Int_Print(int num);
void Window_Print(char const string[], ...);
void Set_More_On(void);
void Set_More_Off(void);

// Window variables
extern int WindowWidth;
extern unsigned int WinB;
extern unsigned int WinC;
extern unsigned int WinX;
extern unsigned int WinY;
extern unsigned int WinCx;
extern unsigned int WinCy;
extern unsigned int WinH;
extern unsigned int WinW;
extern unsigned int Window;
extern int MoreOn;
extern char *TXT_MoreText;
extern void (*Window_More_Ptr)(char const *, int, int, int);

#endif /* WW_WIN_H */

