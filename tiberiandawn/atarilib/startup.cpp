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

bool Read_Private_Config_Struct(char *profile, NewConfigType *config);
void Delete_Swap_Files(void);
void Print_Error_End_Exit(char *string);
void Print_Error_Exit(char *string);
void Check_Use_Compressed_Shapes (void);
void Move_Point(short &x, short &y, register DirType dir, unsigned short distance);

bool VideoBackBufferAllowed = true;
bool SpawnedFromWChat = false;
bool ProgEndCalled = false;
bool RunningAsDLL = false; // Atari ST version is standalone, not a DLL

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

	/*
	** If we are already running then switch to the existing process and exit
	** (Not applicable for Atari ST standalone version)
	*/
	SpawnedFromWChat = false;

	/*
	** Check for sufficient RAM
	*/
	if (Ram_Free(MEM_NORMAL) < 5000000) {
		printf("Insufficient RAM available.\n");
		return(EXIT_FAILURE);
	}

	/*
	** Parse command line arguments
	*/
#ifdef JAPANESE
	ForceEnglish = false;
#endif
	if (Parse_Command_Line(argc, argv)) {

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
		** MMX support not applicable for m68k architecture
		*/
		MMXAvailable = false;

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
			** Initialize audio system
			** TODO: Implement Audio_Init for Atari ST (YM2149/DMA sound)
			*/
			SoundOn = Audio_Init ( NULL , 16 , false , 11025*2 , 0 );

			Palette = new(MEM_CLEAR) unsigned char[768];

			BOOL video_success = FALSE;
			printf("C&C - Setting video mode.\n");
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

			printf("C&C - Initialising video surfaces.\n");

			/*
			** Initialize video buffers
			*/
			if (ScreenWidth==320){
				VisiblePage.Init( ScreenWidth , ScreenHeight , NULL , 0 , (GBC_Enum)0);
				ModeXBuff.Init( ScreenWidth , ScreenHeight , NULL , 0 , (GBC_Enum)(GBC_VISIBLE | GBC_VIDEOMEM));
			} else {
				VisiblePage.Init( ScreenWidth , ScreenHeight , NULL , 0 , (GBC_Enum)0);
				HiddenPage.Init (ScreenWidth , ScreenHeight , NULL , 0 , (GBC_Enum)0);
			}
			ScreenHeight = 1536;

			if (VisiblePage.Get_Height() == 480){
				SeenBuff.Attach(&VisiblePage,0, 40, 1536, 1536);
				HidPage.Attach(&HiddenPage, 0, 40, 1536, 1536);
			}else{
				SeenBuff.Attach(&VisiblePage,0, 0, 1536, 1536);
				HidPage.Attach(&HiddenPage, 0, 0, 1536, 1536);
			}
			printf("C&C - Adjusting variables for resolution.\n");
			Options.Adjust_Variables_For_Resolution();

			printf("C&C - Setting palette.\n");
			/////////Set_Palette(Palette);

			WindowList[0][WINDOWWIDTH] 	= SeenBuff.Get_Width() >> 3;
			WindowList[0][WINDOWHEIGHT]	= SeenBuff.Get_Height();

			/*
			** Install the memory error handler
			*/
			Memory_Error = &Memory_Error_Handler;

			printf("C&C - Creating mouse class.\n");
			WWMouse = new WWMouseClass(&SeenBuff, 32, 32);
			MouseInstalled = TRUE;

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
			if ((stricmp(tempbuff, "No") == 0) || SpawnedFromWChat) {
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
			Prog_End();

			return (EXIT_SUCCESS);

		} else {
			puts("Run SETUP program first.");
			puts("\n");
			Kbd.Get();
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
	if (GameToPlay == GAME_MODEM || GameToPlay == GAME_NULL_MODEM) {
//		NullModem.Change_IRQ_Priority(0);
	}
#endif
	printf("C&C - About to call Sound_End.\n");
	Sound_End();
	printf("C&C - Returned from Sound_End.\n");
	if (WWMouse){
		printf("C&C - Deleting mouse object.\n");
		delete WWMouse;
		WWMouse = NULL;
	}

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
	Get_Key();
	Prog_End();
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

