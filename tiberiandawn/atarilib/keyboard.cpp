/*
 * keyboard.cpp - Keyboard input stubs for Atari ST/MiNT
 * 
 * Provides stub implementations of keyboard functions needed by main source code.
 * These need to be replaced with actual Atari ST keyboard input handling.
 */

#include "keyboard.h"
#include <ctype.h>

/***********************************************************************************************
 * WWKeyboardClass::WWKeyboardClass -- Constructor for the Keyboard Class                    *
 *                                                                                             *
 * INPUT:		none                                                                            *
 *                                                                                             *
 * OUTPUT:     none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/16/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
WWKeyboardClass::WWKeyboardClass()
{
	Head = 0;
	Tail = 0;
	MState = 0;
	Conditional = 0;
	MouseQX = 0;
	MouseQY = 0;
	CurrentCursor = 0;  // NULL equivalent for Atari ST
	// Initialize remap tables (stub - needs proper initialization)
	for (int i = 0; i < 256; i++) {
		VKRemap[i] = (unsigned char)i;
	}
	for (int i = 0; i < 2048; i++) {
		AsciiRemap[i] = (unsigned char)(i & 0xFF);
	}
	/* So Put_Key_Message sets WWKEY_VK_BIT for mouse buttons (see WIN32LIB/KEYBOARD.CPP) */
	AsciiRemap[VK_LBUTTON] = 0;
	AsciiRemap[VK_RBUTTON] = 0;
	AsciiRemap[VK_MBUTTON] = 0;
	for (int i = 0; i < 256; i++) {
		Buffer[i] = 0;
		ToggleKeys[i] = 0;
	}

	_Kbd = this;
}

/***********************************************************************************************
 * WWKeyboardClass::Check -- Checks to see if a key is in the buffer                         *
 *                                                                                             *
 * INPUT:		none                                                                            *
 *                                                                                             *
 * OUTPUT:     BOOL - true if key is available                                                 *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/16/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
BOOL WWKeyboardClass::Check(void)
{
	if (Head == Tail) {
		return FALSE;
	}
	return Buffer[Head];
}

/***********************************************************************************************
 * WWKeyboardClass::Buff_Get -- Lowlevel function to get a key from key buffer                *
 *                                                                                             *
 * INPUT:		none                                                                            *
 *                                                                                             *
 * OUTPUT:     int - the key value that was pulled from buffer                                *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/17/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
int WWKeyboardClass::Buff_Get(void)
{
	while (!Check()) {}										// wait for key in buffer
	int 	temp		= Buffer[Head];						// get key out of the buffer
	int   newhead	= Head;									// save off head for manipulation
	if (Is_Mouse_Key(temp)) {								// if key is a mouse then
		MouseQX	= Buffer[(Head + 1) & 255];			//		get the x and y pos
		MouseQY	= Buffer[(Head + 2) & 255];			//		from the buffer
		newhead += 3;		  									//		adjust head forward
	} else {
		newhead += 1;		  									//		adjust head forward
	}
	newhead	&= 255;
	Head		 = newhead;
	return(temp);
}

/***********************************************************************************************
 * WWKeyboardClass::Is_Mouse_Key -- Checks if a key is a mouse key                           *
 *                                                                                             *
 * INPUT:		int key - the key to check                                                      *
 *                                                                                             *
 * OUTPUT:     BOOL - true if key is a mouse key                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   Stub implementation                                                                       *
 *=============================================================================================*/
BOOL WWKeyboardClass::Is_Mouse_Key(int key)
{
	key &= 0xFF;
	return (key == VK_LBUTTON || key == VK_MBUTTON || key == VK_RBUTTON);
}

/***********************************************************************************************
 * WWKeyboardClass::Put -- Insert a value into the ring buffer (key or mouse coordinate)        *
 *=============================================================================================*/
BOOL WWKeyboardClass::Put(int key)
{
	int temp = (int)((Tail + 1) & 255);
	if (temp != Head) {
		Buffer[Tail] = (unsigned short)key;
		Tail = temp;
		return TRUE;
	}
	return FALSE;
}

/***********************************************************************************************
 * WWKeyboardClass::Put_Key_Message -- Build key bit flags and queue one word (Win32-compatible)*
 *=============================================================================================*/
BOOL WWKeyboardClass::Put_Key_Message(UINT vk_key, BOOL release, BOOL dbl)
{
	int bits = 0;
	/*
	** No GetKeyState on Atari yet; keep modifier bits clear (same as mouse path on Win32).
	*/
	if (vk_key != VK_LBUTTON && vk_key != VK_MBUTTON && vk_key != VK_RBUTTON) {
		/* Placeholder for future IKBD modifier state */
	}
	if (!AsciiRemap[vk_key | bits]) {
		bits |= WWKEY_VK_BIT;
	}
	if (release) {
		bits |= WWKEY_RLS_BIT;
	}
	if (dbl) {
		bits |= WWKEY_DBL_BIT;
	}
	return Put((int)(vk_key | bits));
}

/***********************************************************************************************
 * WWKeyboardClass::Get -- Logic to get a metakey from the buffer                            *
 *                                                                                             *
 * INPUT:		none                                                                            *
 *                                                                                             *
 * OUTPUT:     int - the meta key taken from the buffer                                       *
 *                                                                                             *
 * WARNINGS:	This routine will not return until a keypress is received                      *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/16/1995 PWG : Created.                                                                 *
 *=============================================================================================*/
int WWKeyboardClass::Get(void)
{
	int temp,bits;										// store temp holding spot for key

	while (!Check()) {}								// wait for key in buffer
	temp = Buff_Get();								// get key from the buffer

	bits = temp & 0xFF00;							// save of keyboard bits

	if (!(bits & WWKEY_VK_BIT)) {					// if its not a virtual key
		temp = AsciiRemap[temp&0x1FF] | bits;	//   convert to ascii equivalent
	}
	return(temp);							// return the key that we pulled out
}

// Global keyboard object pointer
WWKeyboardClass *_Kbd = NULL;

int Get_Key_Num(void)
{
	if (!_Kbd)
		return KN_NONE;
	int key = _Kbd->Get();
	int flags = key & 0xFF00;
	key = key & 0x00FF;

	if (isupper(key)) {
		key = tolower(key);
		if (!(flags & WWKEY_VK_BIT)) {
			flags |= WWKEY_SHIFT_BIT;
		}
	}
	return key | flags;
}

int Check_Key(void)
{
	if (!_Kbd)
		return KA_NONE;
	return _Kbd->Check() & ~WWKEY_SHIFT_BIT;
}

int Check_Key_Num(void)
{
	if (!_Kbd)
		return KN_NONE;
	int key = _Kbd->Check();
	int flags = key & 0xFF00;
	key = key & 0x00FF;

	if (isupper(key)) {
		key = tolower(key);
		if (!(flags & WWKEY_VK_BIT)) {
			flags |= WWKEY_SHIFT_BIT;
		}
	}

	return key | flags;
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

void Clear_KeyBuffer(void)
{
	if (!_Kbd)
		return;
	_Kbd->Clear();
}

// Stub: Check if a key is currently down
int Key_Down(int key)
{
	// TODO: Implement key state checking for Atari ST
	return 0; // Key not down
}

void Stuff_Key_Num(int key)
{
	if (_Kbd)
		_Kbd->Put(key);
}

// Stub: Get a key (compatibility function)
int Get_Key(void)
{
	if (!_Kbd) return KN_NONE;
	int retval = _Kbd->Get() & ~WWKEY_SHIFT_BIT;
	if (retval & WWKEY_RLS_BIT) {
		retval = KN_NONE;
	}
	return retval;
}

/***************************************************************************
 * WWKeyboardClass::Clear -- Clears the keyboard buffer                    *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                    *
 *=========================================================================*/
void WWKeyboardClass::Clear(void)
{
	Head = Tail;
}

/***************************************************************************
 * KN_To_VK -- Convert key number to virtual key code                     *
 *                                                                         *
 * INPUT:		int key - key number                                        *
 *                                                                         *
 * OUTPUT:     int - virtual key code                                      *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                    *
 *=========================================================================*/
int KN_To_VK(int key)
{
	if (!_Kbd)
		return KN_NONE;
	if (key & WWKEY_RLS_BIT) {
		return VK_NONE;
	}

	int flags = key & 0xFF00;
	if (!(flags & WWKEY_VK_BIT)) {
		key = _Kbd->VKRemap[key & 0x00FF] | flags;
	}
	key &= ~WWKEY_VK_BIT;
	return key;
}

