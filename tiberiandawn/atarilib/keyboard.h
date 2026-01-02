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
#define	VK_PRINT            0x2A
#define	VK_CAPITAL          0x14
#define	VK_SCROLL           0x91
#define	VK_PAUSE            0x13
#define	VK_SHIFT            0x10
#define	VK_CONTROL          0x11
#define	VK_MENU             0x12
#define	VK_NUMLOCK          0x90
#define	VK_SELECT           0x6C

// KA (ASCII) key codes
enum {
	KA_NONE				= 0,
	KA_MORE 				= 1,
	KA_SETBKGDCOL 		= 2,
	KA_SETFORECOL 		= 6,
	KA_FORMFEED 		= 12,
	KA_SPCTAB 			= 20,
	KA_SETX 				= 25,
	KA_SETY 				= 26,

	KA_SPACE				= 32,
	KA_EXCLAMATION,
	KA_DQUOTE,
	KA_POUND,
	KA_DOLLAR,
	KA_PERCENT,
	KA_AMPER,
	KA_SQUOTE,
	KA_LPAREN,
	KA_RPAREN,
	KA_ASTERISK,
	KA_PLUS,
	KA_COMMA,
	KA_MINUS,
	KA_PERIOD,
	KA_SLASH,

	KA_0, KA_1, KA_2, KA_3, KA_4, KA_5, KA_6, KA_7, KA_8, KA_9,
	KA_COLON,
	KA_SEMICOLON,
	KA_LESS_THAN,
	KA_EQUAL,
	KA_GREATER_THAN,
	KA_QUESTION,

	KA_AT,
	KA_A,
	KA_B,
	KA_C,
	KA_D,
	KA_E,
	KA_F,
	KA_G,
	KA_H,
	KA_I,
	KA_J,
	KA_K,
	KA_L,
	KA_M,
	KA_N,
	KA_O,

	KA_P,
	KA_Q,
	KA_R,
	KA_S,
	KA_T,
	KA_U,
	KA_V,
	KA_W,
	KA_X,
	KA_Y,
	KA_Z,
	KA_LBRACKET,
	KA_BACKSLASH,
	KA_RBRACKET,
	KA_CARROT,
	KA_UNDERLINE,

	KA_GRAVE,
	KA_a,
	KA_b,
	KA_c,
	KA_d,
	KA_e,
	KA_f,
	KA_g,
	KA_h,
	KA_i,
	KA_j,
	KA_k,
	KA_l,
	KA_m,
	KA_n,
	KA_o,

	KA_p,
	KA_q,
	KA_r,
	KA_s,
	KA_t,
	KA_u,
	KA_v,
	KA_w,
	KA_x,
	KA_y,
	KA_z,
	KA_LBRACE,
	KA_BAR,
	KA_RBRACE,
	KA_TILDA,

	KA_ESC 				= VK_ESCAPE | WWKEY_VK_BIT,
	KA_EXTEND 			= VK_ESCAPE | WWKEY_VK_BIT,
	KA_RETURN 			= VK_RETURN | WWKEY_VK_BIT,
	KA_BACKSPACE 		= VK_BACK | WWKEY_VK_BIT,
	KA_TAB 				= VK_TAB  | WWKEY_VK_BIT,
	KA_DELETE			= VK_DELETE | WWKEY_VK_BIT,
	KA_INSERT			= VK_INSERT | WWKEY_VK_BIT,
	KA_PGDN				= VK_NEXT | WWKEY_VK_BIT,
	KA_DOWNRIGHT 		= VK_NEXT | WWKEY_VK_BIT,
	KA_DOWN				= VK_DOWN | WWKEY_VK_BIT,
	KA_END				= VK_END | WWKEY_VK_BIT,
	KA_DOWNLEFT 		= VK_END | WWKEY_VK_BIT,
	KA_RIGHT				= VK_RIGHT | WWKEY_VK_BIT,
	KA_KEYPAD5			= VK_SELECT | WWKEY_VK_BIT,
	KA_LEFT				= VK_LEFT | WWKEY_VK_BIT,
	KA_PGUP				= VK_PRIOR | WWKEY_VK_BIT,
	KA_UPRIGHT 			= VK_PRIOR | WWKEY_VK_BIT,
	KA_UP					= VK_UP | WWKEY_VK_BIT,
	KA_HOME				= VK_HOME | WWKEY_VK_BIT,
	KA_UPLEFT 			= VK_HOME | WWKEY_VK_BIT,
	KA_F12				= VK_F12 | WWKEY_VK_BIT,
	KA_F11				= VK_F11 | WWKEY_VK_BIT,
	KA_F10				= VK_F10 | WWKEY_VK_BIT,
	KA_F9 				= VK_F9 | WWKEY_VK_BIT,
	KA_F8					= VK_F8 | WWKEY_VK_BIT,
	KA_F7					= VK_F7 | WWKEY_VK_BIT,
	KA_F6					= VK_F6 | WWKEY_VK_BIT,
	KA_F5					= VK_F5 | WWKEY_VK_BIT,
	KA_F4					= VK_F4 | WWKEY_VK_BIT,
	KA_F3					= VK_F3 | WWKEY_VK_BIT,
	KA_F2					= VK_F2 | WWKEY_VK_BIT,
	KA_F1					= VK_F1 | WWKEY_VK_BIT,
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
	KN_GRAVE 			= KA_GRAVE,
	KN_1 					= KA_1,
	KN_2 					= KA_2,
	KN_3 					= KA_3,
	KN_4 					= KA_4,
	KN_5 					= KA_5,
	KN_6 					= KA_6,
	KN_7 					= KA_7,
	KN_8 					= KA_8,
	KN_9 					= KA_9,
	KN_0 					= KA_0,
	KN_MINUS 			= KA_MINUS,
	KN_EQUAL 			= KA_EQUAL,
	KN_BACKSPACE		= KA_BACKSPACE,
	KN_TAB				= KA_TAB,
	KN_Q 					= KA_q,
	KN_W 					= KA_w,
	KN_E 					= KA_e,
	KN_R 					= KA_r,
	KN_T 					= KA_t,
	KN_Y 					= KA_y,
	KN_U 					= KA_u,
	KN_I 					= KA_i,
	KN_O 					= KA_o,
	KN_P 					= KA_p,
	KN_LBRACKET			= KA_LBRACKET,
	KN_RBRACKET			= KA_RBRACKET,
	KN_BACKSLASH		= KA_BACKSLASH,
	KN_A 					= KA_a,
	KN_S 					= KA_s,
	KN_D 					= KA_d,
	KN_F 					= KA_f,
	KN_G 					= KA_g,
	KN_H 					= KA_h,
	KN_J 					= KA_j,
	KN_K 					= KA_k,
	KN_L 					= KA_l,
	KN_SEMICOLON 		= KA_SEMICOLON,
	KN_SQUOTE 			= KA_SQUOTE,
	KN_BACKSLASH2 		= KA_BACKSLASH,
	KN_RETURN 			= KA_RETURN,
	KN_Z 					= KA_z,
	KN_X 					= KA_x,
	KN_C 					= KA_c,
	KN_V 					= KA_v,
	KN_B 					= KA_b,
	KN_N 					= KA_n,
	KN_M 					= KA_m,
	KN_COMMA 			= KA_COMMA,
	KN_PERIOD 			= KA_PERIOD,
	KN_SLASH 			= KA_SLASH,
	KN_SPACE 			= KA_SPACE,
	KN_LMOUSE			= KA_LMOUSE,
	KN_RMOUSE			= KA_RMOUSE,
	KN_HOME				= KA_HOME,
	KN_UPLEFT			= KA_UPLEFT,
	KN_LEFT				= KA_LEFT,
	KN_END				= KA_END,
	KN_DOWNLEFT			= KA_DOWNLEFT,
	KN_KEYPAD_SLASH	= KA_SLASH,
	KN_UP					= KA_UP,
	KN_CENTER			= KA_KEYPAD5,
	KN_DOWN				= KA_DOWN,
	KN_INSERT			= KA_INSERT,
	KN_KEYPAD_ASTERISK= KA_ASTERISK,
	KN_PGUP				= KA_PGUP,
	KN_UPRIGHT			= KA_UPRIGHT,
	KN_RIGHT				= KA_RIGHT,
	KN_PGDN				= KA_PGDN,
	KN_DOWNRIGHT		= KA_DOWNRIGHT,
	KN_DELETE			= KA_DELETE,
	KN_KEYPAD_MINUS	= KA_MINUS,
	KN_KEYPAD_PLUS		= KA_PLUS,
	KN_KEYPAD_RETURN	= KA_RETURN,
	KN_ESC				= KA_ESC,
	KN_F1					= KA_F1,
	KN_F2					= KA_F2,
	KN_F3					= KA_F3,
	KN_F4					= KA_F4,
	KN_F5					= KA_F5,
	KN_F6					= KA_F6,
	KN_F7					= KA_F7,
	KN_F8					= KA_F8,
	KN_F9					= KA_F9,
	KN_F10				= KA_F10,
	KN_F11				= KA_F11,
	KN_F12				= KA_F12,
	KN_PRNTSCRN			= VK_PRINT | WWKEY_VK_BIT,
	KN_CAPSLOCK			= VK_CAPITAL | WWKEY_VK_BIT,
	KN_SCROLLLOCK		= VK_SCROLL | WWKEY_VK_BIT,
	KN_PAUSE				= VK_PAUSE | WWKEY_VK_BIT,
	KN_LSHIFT			= VK_SHIFT | WWKEY_VK_BIT,
	KN_RSHIFT			= VK_SHIFT | WWKEY_VK_BIT,
	KN_LCTRL				= VK_CONTROL | WWKEY_VK_BIT,
	KN_RCTRL				= VK_CONTROL | WWKEY_VK_BIT,
	KN_LALT				= VK_MENU | WWKEY_VK_BIT,
	KN_RALT				= VK_MENU | WWKEY_VK_BIT,
	KN_E_INSERT			= VK_INSERT | WWKEY_VK_BIT,
	KN_E_DELETE 		= VK_DELETE | WWKEY_VK_BIT,
	KN_E_LEFT			= VK_LEFT | WWKEY_VK_BIT,
	KN_E_HOME			= VK_HOME | WWKEY_VK_BIT,
	KN_E_END				= VK_END | WWKEY_VK_BIT,
	KN_E_UP				= VK_UP | WWKEY_VK_BIT,
	KN_E_DOWN			= VK_DOWN | WWKEY_VK_BIT,
	KN_E_PGUP			= VK_PRIOR | WWKEY_VK_BIT,
	KN_E_PGDN   		= VK_NEXT | WWKEY_VK_BIT,
	KN_E_RIGHT			= VK_RIGHT | WWKEY_VK_BIT,
	KN_NUMLOCK			= VK_NUMLOCK | WWKEY_VK_BIT,
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
