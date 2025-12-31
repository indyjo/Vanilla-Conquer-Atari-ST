/*
 * keyboard.h - Keyboard input header for Atari ST/MiNT
 * 
 * Provides function declarations for keyboard input functions.
 * This header should be included before jshell.h to provide the needed declarations.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

// Key constants
#ifndef KN_NONE
#define KN_NONE 0
#endif
#ifndef KA_NONE
#define KA_NONE 0
#endif

// Keyboard function declarations - used by jshell.h
// Note: Stuff_Key_Num is also declared in compat.h, so we use extern "C" to avoid conflicts
int Get_Key_Num(void);
int Check_Key_Num(void);
int KN_To_KA(int key);
void Clear_KeyBuffer(void);
int Key_Down(int key);

// Mouse function declarations - used by jshell.h
int Get_Mouse_X(void);
int Get_Mouse_Y(void);

#ifdef __cplusplus
}

// Minimal WWKeyboardClass definition for use in main source
class WWKeyboardClass {
	// Stub - needs implementation
public:
	int MouseQX;
	int MouseQY;
};

#endif

#endif /* KEYBOARD_H */

