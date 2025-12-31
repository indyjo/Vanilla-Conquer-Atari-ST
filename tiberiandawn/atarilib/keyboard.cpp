/*
 * keyboard.cpp - Keyboard input stubs for Atari ST/MiNT
 * 
 * Provides stub implementations of keyboard functions needed by main source code.
 * These need to be replaced with actual Atari ST keyboard input handling.
 */

#include "keyboard.h"

// Global keyboard object pointer (stub - needs implementation)
extern void* _Kbd;

// Key constants
#define KN_NONE 0
#define KA_NONE 0
#define WWKEY_RLS_BIT 0x800
#define WWKEY_VK_BIT 0x1000
#define WWKEY_SHIFT_BIT 0x100

// Stub: Get a key from the keyboard buffer
int Get_Key_Num(void)
{
	// TODO: Implement actual keyboard input for Atari ST
	// For now, return no key
	return KN_NONE;
}

// Stub: Check if a key is available (without removing it)
int Check_Key_Num(void)
{
	// TODO: Implement actual keyboard input for Atari ST
	// For now, return no key
	return KN_NONE;
}

// Stub: Convert key number to ASCII
int KN_To_KA(int key)
{
	// TODO: Implement proper key conversion
	if (key & WWKEY_RLS_BIT) {
		return KA_NONE;
	}
	// Simple conversion - just return the lower byte
	return key & 0xFF;
}

// Stub: Clear the keyboard buffer
void Clear_KeyBuffer(void)
{
	// TODO: Implement keyboard buffer clearing
}

// Stub: Check if a key is currently down
int Key_Down(int key)
{
	// TODO: Implement key state checking for Atari ST
	return 0; // Key not down
}

// Stub: Stuff a key into the keyboard buffer
void Stuff_Key_Num(int key)
{
	// TODO: Implement key stuffing for Atari ST
	(void)key; // Suppress unused parameter warning
}

