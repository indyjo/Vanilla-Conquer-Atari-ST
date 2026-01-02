/*
 * keyboard.h - Keyboard input header for Atari ST/MiNT
 * 
 * This provides a compatible interface to WIN32LIB/keyboard.h
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../COMMONLIB/wwstd.h"
#include "windows.h"

typedef enum {
	WWKEY_SHIFT_BIT	= 0x100,
	WWKEY_CTRL_BIT		= 0x200,
	WWKEY_ALT_BIT		= 0x400,
	WWKEY_RLS_BIT		= 0x800,
	WWKEY_VK_BIT		= 0x1000,
	WWKEY_DBL_BIT		= 0x2000,
	WWKEY_BTN_BIT		= 0x8000,
} WWKey_Type;

class WWKeyboardClass
{
	public:
		WWKeyboardClass();

		BOOL 	Check(void);
		int 	Get(void);
		BOOL 	Put(int key);
		BOOL	Put_Key_Message(	UINT vk_key, BOOL release = FALSE,
										BOOL dbl = FALSE);
		int 	Check_Num(void);
		int 	Get_VK(void);
		int 	Check_ACII(void);
		int 	Get_ASCII(void);
		int 	Check_Bits(void);
		int 	Get_Bits(void);
		int 	To_ASCII(int num);
		int 	Option_On(int option);
		int 	Option_Off(int option);
		void 	Clear(void);
		int 	Down(int key);
		void	AI(void);

		void Message_Handler(HWND hwnd, UINT message, UINT wParam, LONG lParam);

		VOID Split(int &key, int &shift, int &ctrl, int &alt, int &rls, int &dbl);
		BOOL Is_Mouse_Key(int key);

		int				MouseQX;
		int				MouseQY;

		unsigned char	VKRemap[256];
	private:
		int  Buff_Get(void);

		unsigned char	AsciiRemap[2048];
		unsigned short Buffer[256];
		unsigned char  ToggleKeys[256];
		long				Head;
		long				Tail;
		int				MState;
		int				Conditional;
		HANDLE			CurrentCursor;
};

// Virtual key codes
#define	VK_NONE				  0x00
#define	VK_LBUTTON          0x01
#define	VK_RBUTTON          0x02
#define	VK_ESCAPE           0x1B
#define	VK_RETURN           0x0D
#define	VK_LEFT             0x25
#define	VK_RIGHT            0x27
#define	VK_UP               0x26
#define	VK_DOWN             0x28
#define	VK_HOME             0x24
#define	VK_END              0x23
#define	VK_PRIOR            0x21
#define	VK_NEXT             0x22
#define	VK_INSERT           0x2D
#define	VK_DELETE           0x2E
#define	VK_SPACE            0x20
#define	VK_TAB              0x09
#define	VK_BACK             0x08
#define	VK_A                0x41
#define	VK_B                0x42
#define	VK_C                0x43
#define	VK_D                0x44
#define	VK_E                0x45
#define	VK_F                0x46
#define	VK_G                0x47
#define	VK_H                0x48
#define	VK_I                0x49
#define	VK_J                0x4A
#define	VK_K                0x4B
#define	VK_L                0x4C
#define	VK_M                0x4D
#define	VK_N                0x4E
#define	VK_O                0x4F
#define	VK_P                0x50
#define	VK_Q                0x51
#define	VK_R                0x52
#define	VK_S                0x53
#define	VK_T                0x54
#define	VK_U                0x55
#define	VK_V                0x56
#define	VK_W                0x57
#define	VK_X                0x58
#define	VK_Y                0x59
#define	VK_Z                0x5A
#define	VK_0                0x30
#define	VK_1                0x31
#define	VK_2                0x32
#define	VK_3                0x33
#define	VK_4                0x34
#define	VK_5                0x35
#define	VK_6                0x36
#define	VK_7                0x37
#define	VK_8                0x38
#define	VK_9                0x39
#define	VK_F1               0x70
#define	VK_F2               0x71
#define	VK_F3               0x72
#define	VK_F4               0x73
#define	VK_F5               0x74
#define	VK_F6               0x75
#define	VK_F7               0x76
#define	VK_F8               0x77
#define	VK_F9               0x78
#define	VK_F10              0x79
#define	VK_F11              0x7A
#define	VK_F12              0x7B

// KA (ASCII) key codes
enum {
	KA_NONE				= 0,
	KA_ESC 				= VK_ESCAPE | WWKEY_VK_BIT,
	KA_RETURN 			= VK_RETURN | WWKEY_VK_BIT,
	KA_BACKSPACE 		= VK_BACK | WWKEY_VK_BIT,
	KA_TAB 				= VK_TAB  | WWKEY_VK_BIT,
	KA_DELETE			= VK_DELETE | WWKEY_VK_BIT,
	KA_INSERT			= VK_INSERT | WWKEY_VK_BIT,
	KA_PGDN				= VK_NEXT | WWKEY_VK_BIT,
	KA_DOWN				= VK_DOWN | WWKEY_VK_BIT,
	KA_END				= VK_END | WWKEY_VK_BIT,
	KA_RIGHT			= VK_RIGHT | WWKEY_VK_BIT,
	KA_LEFT				= VK_LEFT | WWKEY_VK_BIT,
	KA_PGUP				= VK_PRIOR | WWKEY_VK_BIT,
	KA_UP				= VK_UP | WWKEY_VK_BIT,
	KA_HOME				= VK_HOME | WWKEY_VK_BIT,
	KA_SPACE			= 32,
	KA_LMOUSE 	 		= VK_LBUTTON | WWKEY_VK_BIT,
	KA_RMOUSE 	 		= VK_RBUTTON | WWKEY_VK_BIT,
	KA_SHIFT_BIT 		= WWKEY_SHIFT_BIT,
	KA_CTRL_BIT  		= WWKEY_CTRL_BIT,
	KA_ALT_BIT   		= WWKEY_ALT_BIT,
	KA_RLSE_BIT  		= WWKEY_RLS_BIT,
};

// KN (Numeric) key codes
enum {
	KN_NONE				= 0,
	KN_RETURN 			= KA_RETURN,
	KN_ESC				= KA_ESC,
	KN_LEFT				= KA_LEFT,
	KN_RIGHT			= KA_RIGHT,
	KN_UP				= KA_UP,
	KN_DOWN				= KA_DOWN,
	KN_HOME				= KA_HOME,
	KN_UPLEFT			= KA_HOME,
	KN_END				= KA_END,
	KN_DOWNLEFT			= KA_END,
	KN_PGUP				= KA_PGUP,
	KN_UPRIGHT			= KA_PGUP,
	KN_PGDN				= KA_PGDN,
	KN_DOWNRIGHT		= KA_PGDN,
	KN_INSERT			= KA_INSERT,
	KN_DELETE			= KA_DELETE,
	KN_SPACE 			= KA_SPACE,
	KN_SLASH			= '/' | WWKEY_VK_BIT,
	KN_A				= 'a' | WWKEY_VK_BIT,
	KN_B				= 'b' | WWKEY_VK_BIT,
	KN_C				= 'c' | WWKEY_VK_BIT,
	KN_D				= 'd' | WWKEY_VK_BIT,
	KN_E				= 'e' | WWKEY_VK_BIT,
	KN_F				= 'f' | WWKEY_VK_BIT,
	KN_G				= 'g' | WWKEY_VK_BIT,
	KN_H				= 'h' | WWKEY_VK_BIT,
	KN_I				= 'i' | WWKEY_VK_BIT,
	KN_J				= 'j' | WWKEY_VK_BIT,
	KN_K				= 'k' | WWKEY_VK_BIT,
	KN_L				= 'l' | WWKEY_VK_BIT,
	KN_M				= 'm' | WWKEY_VK_BIT,
	KN_N				= 'n' | WWKEY_VK_BIT,
	KN_O				= 'o' | WWKEY_VK_BIT,
	KN_P				= 'p' | WWKEY_VK_BIT,
	KN_Q				= 'q' | WWKEY_VK_BIT,
	KN_R				= 'r' | WWKEY_VK_BIT,
	KN_S				= 's' | WWKEY_VK_BIT,
	KN_T				= 't' | WWKEY_VK_BIT,
	KN_U				= 'u' | WWKEY_VK_BIT,
	KN_V				= 'v' | WWKEY_VK_BIT,
	KN_W				= 'w' | WWKEY_VK_BIT,
	KN_X				= 'x' | WWKEY_VK_BIT,
	KN_Y				= 'y' | WWKEY_VK_BIT,
	KN_Z				= 'z' | WWKEY_VK_BIT,
	KN_F1				= VK_F1 | WWKEY_VK_BIT,
	KN_F2				= VK_F2 | WWKEY_VK_BIT,
	KN_F3				= VK_F3 | WWKEY_VK_BIT,
	KN_F4				= VK_F4 | WWKEY_VK_BIT,
	KN_F5				= VK_F5 | WWKEY_VK_BIT,
	KN_F6				= VK_F6 | WWKEY_VK_BIT,
	KN_F7				= VK_F7 | WWKEY_VK_BIT,
	KN_F8				= VK_F8 | WWKEY_VK_BIT,
	KN_F9				= VK_F9 | WWKEY_VK_BIT,
	KN_F10				= VK_F10 | WWKEY_VK_BIT,
	KN_F11				= VK_F11 | WWKEY_VK_BIT,
	KN_F12				= VK_F12 | WWKEY_VK_BIT,
	KN_LMOUSE			= KA_LMOUSE,
	KN_RMOUSE			= KA_RMOUSE,
	KN_SHIFT_BIT 		= WWKEY_SHIFT_BIT,
	KN_CTRL_BIT  		= WWKEY_CTRL_BIT | WWKEY_VK_BIT,
	KN_ALT_BIT   		= WWKEY_ALT_BIT | WWKEY_VK_BIT,
	KN_RLSE_BIT  		= WWKEY_RLS_BIT,
	KN_BUTTON    		= WWKEY_BTN_BIT,
};

extern WWKeyboardClass *_Kbd;

/*
** The following routines provide some compatability with the old westwood
** library.
*/
int Check_Key(void);
int Check_Key_Num(void);
int Get_Key_Num(void);
int Get_Key(void);
int KN_To_KA(int key);
void Clear_KeyBuffer(void);
int Key_Down(int key);
int KN_To_VK(int key);

#endif /* KEYBOARD_H */
