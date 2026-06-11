/*
 * st_screen.cpp - Atari ST video: TOS log/phys/rez snapshot, shifter sync ($FF8260),
 * hardware line base ($FF8201/03/0D). With ST_SEPARATE_DEBUG_SCREEN, Ctrl+F10 toggles
 * phys+rez between game buffer and the captured TOS console framebuffer. Without it,
 * the game draws to TOS Logbase and video is set up via Setscreen(). STE palette is not
 * touched here — use Set_Palette etc. from game code; startup snapshots/restores HW pens.
 */

#include "st_screen.h"

#include "ikbd.h"
#include "keyboard.h"
#include "misc.h"

#include <mint/osbind.h>
#include <mint/ostruct.h>

#include <stdio.h>
#include <stdint.h>

enum { ST_LORES_PLANAR_BYTES = 32768 };

#if ST_SEPARATE_DEBUG_SCREEN

static unsigned char *Visible_Alloc = NULL;
static unsigned char *Visible_Plane = NULL;

static void *Alloc_Visible_Plane(int width, int height)
{
	(void)width;
	(void)height;

	if (Visible_Plane) {
		return Visible_Plane;
	}

	Visible_Alloc = new unsigned char[ST_LORES_PLANAR_BYTES + 256];
	if (!Visible_Alloc) {
		return NULL;
	}

	uintptr_t raw = (uintptr_t)Visible_Alloc;
	Visible_Plane = (unsigned char *)((raw + 255u) & ~(uintptr_t)255u);
	return Visible_Plane;
}

static void Free_Visible_Plane(void)
{
	delete[] Visible_Alloc;
	Visible_Alloc = NULL;
	Visible_Plane = NULL;
}

/*
 * ST shifter sync (shift mode): $FF8260 low byte — same convention as TOS Getrez():
 * 0 = low 320×200×16, 1 = medium 640×200×4, 2 = high 640×400×2 mono.
 * STE uses extra bits; we only touch the classic ST mode bits.
 */
#define ST_SHIFTER_SYNC_MODE ((volatile unsigned char *)0xFF8260UL)

static void ST_Shifter_Set_Sync_Mode_Only(int rez)
{
	unsigned char v = (unsigned char)((unsigned)rez & 3u);
	if (v > 2u) {
		v = 2u;
	}
	Wait_Vert_Blank();
	*ST_SHIFTER_SYNC_MODE = v;
}

static unsigned long Tos_LogBase = 0;
static unsigned long Tos_PhysBase = 0;
static int Tos_Rez = 0;
static int Tos_StateCaptured = 0;

static int OriginalResolution = -1;
static int ResolutionChanged = 0;

static void *Game_Visible_Planar = NULL;

/* True: hardware shows TOS console; false: game framebuffer + low res. */
static int Tos_Console_Visible = 0;
static int Console_Hotkey_Prev = 0;

static void Switch_To_LoRes(void)
{
	int current_rez = Getrez();

	if (OriginalResolution < 0) {
		OriginalResolution = current_rez;
	}

	if (current_rez == 0) {
		return;
	}

	ST_Shifter_Set_Sync_Mode_Only(0);

	int new_rez = Getrez();
	if (new_rez == 0) {
		ResolutionChanged = 1;
		printf("C&C - Switched to LoRes mode (320x200).\n");
	} else {
		printf("C&C - Warning: Could not switch to LoRes mode. Current mode: %d\n", new_rez);
	}
}

static void Restore_Original_Resolution(void)
{
	if (ResolutionChanged && OriginalResolution >= 0) {
		ST_Shifter_Set_Sync_Mode_Only(OriginalResolution);
		ResolutionChanged = 0;
		printf("C&C - Restored original resolution mode: %d\n", OriginalResolution);
	}
}

/*------------------------------------------------------------------------*/
/* Public ST screen API                                                   */
/*------------------------------------------------------------------------*/

void ST_Screen_Capture_Tos_Video_State(void)
{
	if (Tos_StateCaptured) {
		return;
	}
	Tos_LogBase = (unsigned long)Logbase();
	Tos_PhysBase = (unsigned long)Physbase();
	Tos_Rez = Getrez();
	Tos_StateCaptured = 1;
	printf("C&C - Saved TOS video: log=$%lX phys=$%lX rez=%d\n",
		Tos_LogBase, Tos_PhysBase, Tos_Rez);
}

int ST_Screen_Enter_LoRes_Game_Video(void)
{
	Switch_To_LoRes();

	int rez = Getrez();
	if (rez != 0) {
		printf("C&C - Error: Not in Lorez (low resolution) mode. Current mode: %d\n", rez);
		printf("C&C - Please switch to low resolution (320x200) mode.\n");
		return 0;
	}

	Cursconf(CURS_HIDE, 0);

	return 1;
}

void *ST_Screen_Register_Game_Visible(int width, int height)
{
	void *plane = Alloc_Visible_Plane(width, height);
	Game_Visible_Planar = plane;
	return plane;
}

void ST_Screen_Shutdown_Restore_Tos(void)
{
	Restore_Original_Resolution();

	if (Tos_StateCaptured) {
		ST_Shifter_Set_Sync_Mode_Only(Tos_Rez);
		ST_Screen_Hardware_Set_Phys_Base((void *)Tos_PhysBase);
		printf("C&C - Restored TOS shifter: phys=$%lX rez=%d (log=$%lX unchanged in OS).\n",
			Tos_PhysBase, Tos_Rez, Tos_LogBase);
	}

	Free_Visible_Plane();
	Game_Visible_Planar = NULL;
}

void ST_Screen_Hardware_Set_Phys_Base(void *phys)
{
	uintptr_t a = (uintptr_t)phys;
	if (a == 0u) {
		return;
	}

	Wait_Vert_Blank();

	volatile unsigned char *const p_hi = (volatile unsigned char *)0xFF8201UL;
	volatile unsigned char *const p_mid = (volatile unsigned char *)0xFF8203UL;
	volatile unsigned char *const p_low = (volatile unsigned char *)0xFF820DUL;

	*p_hi = (unsigned char)((a >> 16) & 0xFFu);
	*p_mid = (unsigned char)((a >> 8) & 0xFFu);
	*p_low = (unsigned char)(a & 0xFFu);
}

static void Apply_Game_Video_Hardware(void)
{
	if (!Game_Visible_Planar) {
		return;
	}
	ST_Shifter_Set_Sync_Mode_Only(0);
	ST_Screen_Hardware_Set_Phys_Base(Game_Visible_Planar);
	Tos_Console_Visible = 0;
}

void ST_Screen_Apply_Game_Video_Hardware(void)
{
	Apply_Game_Video_Hardware();
}

static void Apply_Tos_Console_Video_Hardware(void)
{
	if (!Tos_StateCaptured || Tos_PhysBase == 0u) {
		return;
	}
	ST_Shifter_Set_Sync_Mode_Only(Tos_Rez);
	ST_Screen_Hardware_Set_Phys_Base((void *)Tos_PhysBase);
	Tos_Console_Visible = 1;
}

void ST_Debug_Screen_Service(void)
{
	IKBD_Service();

	if (!Game_Visible_Planar || !Tos_StateCaptured) {
		return;
	}

	int down = (IKBD_Key_Is_Down(VK_CONTROL) && IKBD_Key_Is_Down(VK_F10)) ? 1 : 0;
	if (down && !Console_Hotkey_Prev) {
		if (Tos_Console_Visible) {
			Apply_Game_Video_Hardware();
		} else {
			Apply_Tos_Console_Video_Hardware();
		}
	}
	Console_Hotkey_Prev = down;
}

int ST_Debug_Screen_Is_Active(void)
{
	return Tos_Console_Visible ? 1 : 0;
}

#else /* !ST_SEPARATE_DEBUG_SCREEN */

static long Tos_LogBase = 0;
static long Tos_PhysBase = 0;
static int Tos_Rez = 0;
static int Tos_StateCaptured = 0;

void ST_Screen_Capture_Tos_Video_State(void)
{
	if (Tos_StateCaptured) {
		return;
	}
	Tos_LogBase = Logbase();
	Tos_PhysBase = Physbase();
	Tos_Rez = Getrez();
	Tos_StateCaptured = 1;
	printf("C&C - Saved TOS video: log=$%lX phys=$%lX rez=%d\n",
		Tos_LogBase, Tos_PhysBase, Tos_Rez);
}

int ST_Screen_Enter_LoRes_Game_Video(void)
{
	Setscreen(-1L, -1L, 0);

	int rez = Getrez();
	if (rez != 0) {
		printf("C&C - Error: Not in Lorez (low resolution) mode. Current mode: %d\n", rez);
		printf("C&C - Please switch to low resolution (320x200) mode.\n");
		return 0;
	}

	Cursconf(CURS_HIDE, 0);

	return 1;
}

void *ST_Screen_Register_Game_Visible(int width, int height)
{
	(void)width;
	(void)height;
	return (void *)Logbase();
}

void ST_Screen_Shutdown_Restore_Tos(void)
{
	if (Tos_StateCaptured) {
		Setscreen(Tos_LogBase, Tos_PhysBase, (long)Tos_Rez);
		printf("C&C - Restored TOS video: log=$%lX phys=$%lX rez=%d\n",
			Tos_LogBase, Tos_PhysBase, Tos_Rez);
	}
}

void ST_Screen_Hardware_Set_Phys_Base(void *phys)
{
	(void)phys;
}

void ST_Screen_Apply_Game_Video_Hardware(void)
{
}

void ST_Debug_Screen_Service(void)
{
}

int ST_Debug_Screen_Is_Active(void)
{
	return 0;
}

#endif /* ST_SEPARATE_DEBUG_SCREEN */
