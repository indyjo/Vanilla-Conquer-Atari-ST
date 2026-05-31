//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free 
// software: you can redistribute it and/or modify it under the terms of 
// the GNU General Public License as published by the Free Software Foundation, 
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed 
// in the hope that it will be useful, but with permitted additional restrictions 
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT 
// distributed with this program. You should have received a copy of the 
// GNU General Public License along with permitted additional restrictions 
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/* $Header:   F:\projects\c&c\vcs\code\startup.cpv   2.17   16 Oct 1995 16:48:12   JOE_BOSTIC  $ */
/***********************************************************************************************
 ***             C O N F I D E N T I A L  ---  W E S T W O O D   S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : STARTUP.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : October 3, 1994                                              *
 *                                                                                             *
 *                  Last Update : August 27, 1995 [JLB]                                        *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Delete_Swap_Files -- Deletes previously existing swap files.                              *
 *   Prog_End -- Cleans up library systems in prep for game exit.                              *
 *   main -- Initial startup routine (preps library systems).                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include	"function.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdint.h>
#include	<mint/osbind.h>  // For XBIOS functions: Getrez, Cursconf
#include	<mint/ostruct.h> // CURS_HIDE, CURS_SHOW
#include	<mint/linea.h>  // For LINE-A initialization (linea2, __aline)
#include	"gbuffer.h"  // GBC_ST_PLANAR_LORES, Uses_ST_LoRes_Planar_Layout
#include	"ikbd.h"
#include	"st_screen.h"
#include	"palette.h"

static BOOL Require_ST_Blitter(void)
{
	short cfg = Blitmode(-1);
	if ((cfg & 0x0002) == 0) {
		return FALSE;
	}
	/* Force hardware blitter mode globally. */
	Blitmode(BLIT_HARD);
	return TRUE;
}

// Local function declarations (not in headers)
void Delete_Swap_Files(void);
void Print_Error_End_Exit(char *string);
void Print_Error_Exit(char *string);
void Check_Use_Compressed_Shapes (void);
void Move_Point(short &x, short &y, register DirType dir, unsigned short distance);
void Prog_End(const char *why, bool fatal);
void Read_Setup_Options(RawFileClass *config_file);
BOOL Set_Video_Mode(void *hwnd, int w, int h, int bits_per_pixel);

bool VideoBackBufferAllowed = true;
bool SpawnedFromWChat = false;
bool ProgEndCalled = false;
// RunningAsDLL is defined in globals.cpp, just declare it here
extern bool RunningAsDLL;

extern bool ReadyToQuit;

/***********************************************************************************************
 * main -- Initial startup routine (preps library systems).                                    *
 *                                                                                             *
 *    This is the routine that is first called when the program starts up. It basically        *
 *    handles the command line parsing and setting up library systems.                         *
 *                                                                                             *
 * INPUT:   argc  -- Number of command line arguments.                                         *
 *                                                                                             *
 *          argv  -- Pointer to array of comman line argument strings.                         *
 *                                                                                             *
 * OUTPUT:  Returns with execution failure code (if any).                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/20/1995 JLB : Created.                                                                 *
 *=============================================================================================*/

int main(int argc, char *argv[])
{
	printf("C&C - Starting up.\n");

	{
		char st_ram_boot_msg[192];
		long const st_largest = Ram_Free(MEM_NORMAL);
		long const tt_largest = Total_Ram_Free(MEM_NORMAL) - st_largest;
		snprintf(st_ram_boot_msg, sizeof(st_ram_boot_msg),
			"C&C ST - At program start: largest free ST-RAM block %ld bytes (~%ld KiB); "
			"largest TT-RAM block %ld bytes (~%ld KiB).\n",
			st_largest, (st_largest > 0L) ? (st_largest / 1024L) : 0L,
			tt_largest, (tt_largest > 0L) ? (tt_largest / 1024L) : 0L);
		printf("%s", st_ram_boot_msg);
		fflush(stdout);
	}

	/*
	** Enable supervisor mode early for Atari ST
	** This is required for direct hardware access (palette registers, etc.)
	** This executable stays supervisor for its lifetime — no paired Super(save) exit.
	** Any future Super(0)/return pair in port code must use SuperToUser(save), not Super(save),
	** on plain TOS/EmuTOS (stack vs USP bookkeeping); see mint/osbind.h SuperToUser commentary.
	*/
	Super(0L);

	/*
	** Initialize LINE-A system immediately after supervisor mode
	** This sets up __aline which is required for CUR_X/CUR_Y mouse position access
	*/
	linea0();

	/*
	** If we are already running then switch to the existing process and exit
	** (Not applicable for Atari ST standalone version)
	*/
	SpawnedFromWChat = false;

	/*
	** Check for sufficient RAM
	*/
#ifdef WIN32
	if (Ram_Free(MEM_NORMAL) < 5000000) {
		printf("Insufficient RAM available.\n");
		return(EXIT_FAILURE);
	}
#endif

	/*
	** Parse command line arguments
	*/
#ifdef JAPANESE
	ForceEnglish = false;
#endif
	if (Parse_Command_Line(argc, argv)) {

		/*
		** Use the current working directory for config, saves, and file search.
		*/
		CDFileClass::Refresh_Search_Drives();

		/*
		** Initialize timer system (60 Hz)
		** TODO: Implement TimerClass for Atari ST
		*/
		// Timer initialization will be handled by Init_Game()

		RawFileClass cfile("CONQUER.INI");

#ifdef JAPANESE
		//////////////////////////////////////if(!ForceEnglish) KBLanguage = 1;
#endif

		/*
		** If there is loads of memory then use uncompressed shapes
		*/
		Check_Use_Compressed_Shapes();

		/*
		** If there is not enough disk space free, dont allow the product to run.
		*/
		if (Disk_Space_Available() < INIT_FREE_DISK_SPACE) {
			printf("Warning - you are critically low on free disk space for virtual memory and save games.\n");
			// For Atari ST, we'll continue anyway but warn the user
		}

		if (cfile.Is_Available()) {

#ifndef NOMEMCHECK
			char * cdata = (char *)Load_Alloc_Data(cfile);
			Read_Private_Config_Struct(cdata, &NewConfig);
			delete [] cdata;
#else
			Read_Private_Config_Struct((char *)Load_Alloc_Data(cfile), &NewConfig);
#endif
			Read_Setup_Options( &cfile );

			printf("C&C - Initialising audio.\n");

			/*
			** Initialize audio system (STe-class DMA 8-bit mono in audio_ste.cpp).
			*/
			SoundOn = Audio_Init ( NULL , 8 , false , 11025*2 , 0 );
			if (!SoundOn) {
				printf("C&C - Failed to initialize audio.\n");
			}

			Palette = new(MEM_CLEAR) unsigned char[768];

			BOOL video_success = FALSE;
			printf("C&C - Setting video mode.\n");
#ifdef ATARI_ST
			ScreenWidth = 320;
			ScreenHeight = 200;
#endif
			/*
			** Set video mode for Atari ST
			** TODO: Implement Set_Video_Mode for Atari ST (VDI/XBIOS)
			*/
			if (ScreenHeight == 400){
				if (Set_Video_Mode (NULL, ScreenWidth, ScreenHeight, 8)){
					video_success = TRUE;
				}else{
					if (Set_Video_Mode (NULL, ScreenWidth, 480, 8)){
						video_success = TRUE;
						ScreenHeight = 480;
					}
				}
			}else{
				if (Set_Video_Mode (NULL, ScreenWidth, ScreenHeight, 8)){
					video_success = TRUE;
				}
			}

			if (!video_success){
				printf("C&C - Failed to set video mode.\n");
				if (Palette) delete [] Palette;
				return (EXIT_FAILURE);
			}
			if (!Require_ST_Blitter()) {
				printf("C&C - Atari BLiTTER chip not available. This build requires BLiTTER hardware.\n");
				if (Palette) delete [] Palette;
				return (EXIT_FAILURE);
			}

			printf("C&C - Initialising video surfaces.\n");
			printf("C&C - ScreenWidth: %d, ScreenHeight: %d\n", ScreenWidth, ScreenHeight);

			/*
			** Initialize video buffers
			** ST LoRes: allocate separate visible + hidden planar pages (320x200 only).
			** Other resolutions keep linear 8bpp + per-frame C2P fallback.
			*/
			if (ScreenWidth == 320 && ScreenHeight == 200) {
				/*
				 * ST shifter uses 256-byte-aligned video base. Allocate separate visible + hidden
				 * planar pages in ST-RAM. TOS keeps Logbase/Physbase on its original screen for
				 * console output; the game points the shifter at its buffer via $FF8201/$FF8203/
				 * $FF820D and $FF8260 (st_screen.cpp), not Setscreen, so TOS keeps rendering glyphs
				 * into the shell buffer.
				 */
				static unsigned char *st_visible_alloc = NULL;
				static unsigned char *st_hidden_alloc = NULL;
				static unsigned char *st_visible_plane = NULL;
				static unsigned char *st_hidden_plane = NULL;
				if (!st_visible_alloc) {
					st_visible_alloc = new unsigned char[32768 + 256];
					uintptr_t raw_v = (uintptr_t)st_visible_alloc;
					st_visible_plane = (unsigned char *)((raw_v + 255u) & ~(uintptr_t)255u);
				}
				if (!st_hidden_alloc) {
					st_hidden_alloc = new unsigned char[32768 + 256];
					uintptr_t raw_h = (uintptr_t)st_hidden_alloc;
					st_hidden_plane = (unsigned char *)((raw_h + 255u) & ~(uintptr_t)255u);
				}
				VisiblePage.Init(320, 200, st_visible_plane, 32768, (GBC_Enum)GBC_ST_PLANAR_LORES);
				HiddenPage.Init(320, 200, st_hidden_plane, 32768, (GBC_Enum)GBC_ST_PLANAR_LORES);
				VisiblePage.Clear(0);
				HiddenPage.Clear(0);
				ST_Screen_Register_Game_Visible(VisiblePage.Get_Buffer(), 320, 200);
				ST_Screen_Apply_Game_Video_Hardware();
			} else {
				VisiblePage.Init( ScreenWidth , ScreenHeight , NULL , 0 , (GBC_Enum)0);
				HiddenPage.Init (ScreenWidth , ScreenHeight , NULL , 0 , (GBC_Enum)0);
			}

			if (VisiblePage.Get_Height() == 480){
				SeenBuff.Attach(&VisiblePage,0, 40, ScreenWidth, 400);
				HidPage.Attach(&HiddenPage, 0, 40, ScreenWidth, 400);
			}else{
				SeenBuff.Attach(&VisiblePage,0, 0, ScreenWidth, ScreenHeight);
				HidPage.Attach(&HiddenPage, 0, 0, ScreenWidth, ScreenHeight);
			}
			printf("C&C - Adjusting variables for resolution.\n");
			Options.Adjust_Variables_For_Resolution();

			printf("C&C - Setting palette.\n");
			/////////Set_Palette(Palette);

			WindowList[0][WINDOWWIDTH] 	= SeenBuff.Get_Width();
			WindowList[0][WINDOWHEIGHT]	= SeenBuff.Get_Height();

			/*
			** Install the memory error handler
			*/
			Memory_Error = &Memory_Error_Handler;

			printf("C&C - Creating mouse class.\n");
			WWMouse = new WWMouseClass(&SeenBuff, 32, 32);
			MouseInstalled = TRUE;
			IKBD_Install();
			Keyboard = CreateWWKeyboardClass();

			/*
			** See if we should run the intro
			*/
			printf("C&C - Reading CONQUER.INI.\n");
			char *buffer = (char*)Alloc(64000 , MEM_NORMAL);
			cfile.Read(buffer, cfile.Size());
			buffer[cfile.Size()] = '\0';

			/*
			**	Check for forced intro movie run disabling. If the conquer
			**	configuration file says "no", then don't run the intro.
			*/
			char tempbuff[5];
			WWGetPrivateProfileString("Intro", "PlayIntro", "Yes", tempbuff, 4, buffer);
			if ((_stricmp(tempbuff, "No") == 0) || SpawnedFromWChat) {
				Special.IsFromInstall = false;
			}else{
				Special.IsFromInstall = true;
			}
			SlowPalette = WWGetPrivateProfileInt("Options", "SlowPalette", 1, buffer);

#ifdef DEMO
			/*
			**	Check for override directory path for CD searches.
			*/
			WWGetPrivateProfileString("CD", "Path", ".", OverridePath, sizeof(OverridePath), buffer);
#endif

			/*
			** Regardless of whether we should run it or not, here we're
			** gonna change it to say "no" in the future.
			*/
			WWWritePrivateProfileString("Intro", "PlayIntro", "No", buffer);
			cfile.Write(buffer, strlen(buffer));

			Free(buffer);

			/*
			**	If the intro is being run for the first time, then don't
			**	allow breaking out of it with the <ESC> key.
			*/
			if (Special.IsFromInstall) {
				BreakoutAllowed = false;
			}

			Memory_Error_Exit = Print_Error_End_Exit;

			printf("C&C - Entering main game.\n");
			Main_Game(argc, argv);

			VisiblePage.Clear();
			HiddenPage.Clear();

			Memory_Error_Exit = Print_Error_Exit;

			printf("C&C - About to exit.\n");
			ReadyToQuit = 1;

			/*
			** Cleanup
			*/
			Prog_End(NULL, false);

			return (EXIT_SUCCESS);

		} else {
			puts("Run SETUP program first.");
			puts("\n");
			getchar();
		}

		if (Palette){
			delete [] Palette;
			Palette = NULL;
		}
	}

	return(EXIT_SUCCESS);
}


/***********************************************************************************************
 * Prog_End -- Cleans up library systems in prep for game exit.                                *
 *                                                                                             *
 *    This routine should be called before the game terminates. It handles cleaning up         *
 *    library systems so that a graceful return to the host operating system is achieved.      *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/20/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void Prog_End(const char *why, bool fatal)
{
	if (why) {
		printf("Prog_End: %s\n", why);
	}
	if (fatal) {
		abort();
	}
	
#ifndef DEMO
	// Modem/Null-modem cleanup (not needed for Atari ST)
	// if (GameToPlay == GAME_MODEM || GameToPlay == GAME_NULL_MODEM) {
	//		NullModem.Change_IRQ_Priority(0);
	// }
#endif
	printf("C&C - About to call Sound_End.\n");
	IKBD_Uninstall();
	Sound_End();
	printf("C&C - Returned from Sound_End.\n");
	if (WWMouse){
		printf("C&C - Deleting mouse object.\n");
		WWMouseClass *mouse_ptr = WWMouse;
		delete mouse_ptr;
		WWMouse = NULL;
	}

	Palette_ST_Restore_Hardware_State_And_Clear();
	ST_Screen_Shutdown_Restore_Tos();

	Cursconf(CURS_SHOW, 0);

	if (Palette){
		printf("C&C - Deleting palette object.\n");
		delete [] Palette;
		Palette = NULL;
	}

	ProgEndCalled = true;
}


/***********************************************************************************************
 * Delete_Swap_Files -- Deletes previously existing swap files.                                *
 *                                                                                             *
 *    This routine will scan through the current directory and delete any swap files it may    *
 *    find. This is used to clear out any left over swap files from previous runs (crashes)    *
 *    of the game. This routine presumes that it cannot delete the swap file that is created   *
 *    by the current run of the game.                                                          *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   08/27/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void Delete_Swap_Files(void)
{
	// TODO: Implement for Atari ST if needed
}


void Print_Error_End_Exit(char *string)
{
	printf( "%s\n", string );
	if (Keyboard) {
		Keyboard->Get();
	} else {
		getchar();
	}
	Prog_End(string, true);
	printf( "%s\n", string );
	if (!RunningAsDLL) {
		exit(1);
	}
}


void Print_Error_Exit(char *string)
{
	printf( "%s\n", string );
	if (!RunningAsDLL) {
		exit(1);
	}
}

/***********************************************************************************************
 * Read_Setup_Options -- Read stuff in from the INI file that we need to know sooner           *
 *                                                                                             *
 * INPUT:    config_file -- Pointer to the config file                                        *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    6/7/96 4:09PM ST : Created                                                               *
 *=============================================================================================*/
void Read_Setup_Options( RawFileClass *config_file )
{
	char *buffer = new char [config_file->Size()];

	if (config_file->Is_Available()){

		config_file->Read (buffer, config_file->Size());

		VideoBackBufferAllowed = WWGetPrivateProfileInt ("Options", "VideoBackBuffer", 1, buffer);
		AllowHardwareBlitFills = WWGetPrivateProfileInt ("Options", "HardwareFills", 1, buffer);
		//ScreenHeight = WWGetPrivateProfileInt ("Options", "Resolution", 0, buffer) ? 1536 : 1536;
		IsV107 = WWGetPrivateProfileInt ("Options", "Compatibility", 0, buffer);
	}


	delete [] buffer;
}

/***********************************************************************************************
 * Set_Video_Mode -- Sets the video mode for Atari ST                                          *
 *                                                                                             *
 * INPUT:   hwnd            -- Window handle (unused on Atari ST)                             *
 *         w                -- Width in pixels                                                 *
 *         h                -- Height in pixels                                                 *
 *         bits_per_pixel   -- Bits per pixel (8 for 256 colors)                              *
 *                                                                                             *
 * OUTPUT:  TRUE if successful, FALSE otherwise                                                *
 *                                                                                             *
 * WARNINGS: Fails if not in Lorez (low resolution) mode                                       *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    Created for Atari ST port                                                               *
 *=============================================================================================*/
BOOL Set_Video_Mode(void *hwnd, int w, int h, int bits_per_pixel)
{
	(void)hwnd;
	(void)w;
	(void)h;
	(void)bits_per_pixel;

	ST_Screen_Capture_Tos_Video_State();
	if (!ST_Screen_Enter_LoRes_Game_Video()) {
		return FALSE;
	}
	Palette_ST_Capture_Hardware_State_Once();
	return TRUE;
}

// Global variable: Share ally visibility (from WIN32LIB/DLLInterface.cpp)
bool ShareAllyVisibility = true;

// Global variable: Window number (from display.cpp) - matches ww_win.h declaration
unsigned int Window = WINDOW_MAIN;

// Global variable: GlyphX client sidebar width (from WIN32LIB/DLLInterface.cpp)
int GlyphXClientSidebarWidthInLeptons = 0;

// Global variable: Total locks (from conquer.cpp)
int TotalLocks = 0;

/***************************************************************************
 * Change_Window -- Change the current window                              *
 *                                                                         *
 * INPUT:		int windnum - window number                                *
 *                                                                         *
 * OUTPUT:     int - window number (matches ww_win.h declaration)        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                    *
 *=========================================================================*/
int Change_Window(int windnum)
{
	Window = (unsigned int)windnum;
	// TODO: Implement actual window switching for Atari ST
	return windnum;
}

/***************************************************************************
 * Window_Hide_Mouse -- Hide mouse for a specific window                   *
 *                                                                         *
 * INPUT:		int window - window number                                 *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                    *
 *=========================================================================*/
void Window_Hide_Mouse(int window)
{
	// Stub for Atari ST - just hide the mouse
	(void)window;
	Hide_Mouse();
}

/***************************************************************************
 * Window_Show_Mouse -- Show mouse for current window                      *
 *                                                                         *
 * INPUT:		none                                                        *
 *                                                                         *
 * OUTPUT:     none                                                        *
 *                                                                         *
 * HISTORY:                                                                *
 *   Stub for Atari ST                                                    *
 *=========================================================================*/
void Window_Show_Mouse(void)
{
	// Stub for Atari ST - just show the mouse
	Show_Mouse();
}

