/*
 * mouseww.cpp - Mouse input stubs for Atari ST/MiNT
 * 
 * Provides stub implementations of mouse functions needed by main source code.
 * These need to be replaced with actual Atari ST mouse input handling.
 */

// Global mouse object pointer (stub - needs implementation)
extern void* _Mouse;

// DLL force mouse position (used when mouse is controlled externally)
extern int DLLForceMouseX;
extern int DLLForceMouseY;

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

