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
#include	"palette.h"  // For PaletteToST mapping array
#include	"c2p.h"
#include	"st_temperat_palette.h"
#include	"gbuffer.h"  // GBC_ST_PLANAR_LORES, Uses_ST_LoRes_Planar_Layout
#include	"misc.h"     // Wait_Vert_Blank

// Atari ST palette hardware register addresses
// Palette registers are at $FF8240-$FF825E (16 registers, 16-bit each, 2 bytes apart)
#define PALETTE_BASE_ADDR 0xFF8240
#define PALETTE_REG_COUNT 16

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

// Pointer to palette hardware registers (volatile because hardware can change them)
static volatile unsigned short *PaletteRegs = (volatile unsigned short *)PALETTE_BASE_ADDR;

// Global variable to store original palette for restoration
// Atari ST palette is 16 words (16-bit each)
static unsigned short SavedOriginalPalette[16];
static bool OriginalPaletteSaved = false;

// Global variable to store original resolution for restoration
static int OriginalResolution = -1;
static bool ResolutionChanged = false;

// Local function declarations (not in headers)
bool Read_Private_Config_Struct(char *profile, NewConfigType *config);
void Delete_Swap_Files(void);
void Print_Error_End_Exit(char *string);
void Print_Error_Exit(char *string);
void Check_Use_Compressed_Shapes (void);
void Move_Point(short &x, short &y, register DirType dir, unsigned short distance);
void Prog_End(const char *why, bool fatal);
void Read_Setup_Options(RawFileClass *config_file);
BOOL Set_Video_Mode(void *hwnd, int w, int h, int bits_per_pixel);

// Atari ST palette helper functions
static void Save_Original_Palette(void);
static void Restore_Original_Palette(void);
static void Init_Temperat_HW_Palette(void);

// Atari ST resolution helper functions
static void Switch_To_LoRes(void);
static void Restore_Original_Resolution(void);

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

	/*
	** Enable supervisor mode early for Atari ST
	** This is required for direct hardware access (palette registers, etc.)
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
			if (!Require_ST_Blitter()) {
				printf("C&C - Atari BLiTTER chip not available. This build requires BLiTTER hardware.\n");
				if (Palette) delete [] Palette;
				return (EXIT_FAILURE);
			}

			printf("C&C - Initialising video surfaces.\n");
			printf("C&C - ScreenWidth: %d, ScreenHeight: %d\n", ScreenWidth, ScreenHeight);

			/*
			** Initialize video buffers
			** ST LoRes: recycle TOS logical screen for VisiblePage and allocate one extra
			** 32KiB aligned planar page for HiddenPage (320x200 only).
			** Other resolutions keep linear 8bpp + per-frame C2P fallback.
			*/
			if (ScreenWidth == 320 && ScreenHeight == 200) {
				/*
				 * ST shifter uses a 256-byte-aligned video base (low 8 bits ignored). We reuse
				 * current TOS screen as visible and allocate one aligned hidden page for drawing.
				 */
				static unsigned char *st_plane_alloc = NULL;
				static unsigned char *st_hidden_plane = NULL;
				if (!st_plane_alloc) {
					st_plane_alloc = new unsigned char[32768 + 256];
					uintptr_t raw = (uintptr_t)st_plane_alloc;
					st_hidden_plane = (unsigned char *)((raw + 255u) & ~(uintptr_t)255u);
				}
				unsigned char *tos_visible = (unsigned char *)Logbase();
				VisiblePage.Init(320, 200, tos_visible, 32768, (GBC_Enum)GBC_ST_PLANAR_LORES);
				HiddenPage.Init(320, 200, st_hidden_plane, 32768, (GBC_Enum)GBC_ST_PLANAR_LORES);
				VisiblePage.Clear(0);
				HiddenPage.Clear(0);
				Setscreen((long)VisiblePage.Get_Buffer(), (long)VisiblePage.Get_Buffer(), -1);
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
	// Modem/Null-modem cleanup (not needed for Atari ST)
	// if (GameToPlay == GAME_MODEM || GameToPlay == GAME_NULL_MODEM) {
	//		NullModem.Change_IRQ_Priority(0);
	// }
#endif
	printf("C&C - About to call Sound_End.\n");
	Sound_End();
	printf("C&C - Returned from Sound_End.\n");
	if (WWMouse){
		printf("C&C - Deleting mouse object.\n");
		WWMouseClass *mouse_ptr = WWMouse;
		delete mouse_ptr;
		WWMouse = NULL;
	}

	// Restore original palette before cleanup
	Restore_Original_Palette();
	
	// Restore original resolution before cleanup
	Restore_Original_Resolution();

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
	Get_Key();
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

		/*
		** See if an alternative socket number has been specified
		*/
		int socket = WWGetPrivateProfileInt ("Options", "Socket", 0, buffer);
		if (socket >0 ){
			socket += 0x4000;
			if (socket >= 0x4000 && socket < 0x8000) {
				Ipx.Set_Socket (socket);
			}
		}

		/*
		** See if a destination network has been specified
		*/
		char netbuf [512];
		memset (netbuf, 0, sizeof (netbuf) );
		char *netptr = WWGetPrivateProfileString ("Options", "DestNet", NULL, netbuf, sizeof (netbuf), buffer);

		if (netptr && strlen (netbuf)){
			NetNumType net;
			NetNodeType node;

			/*
			** Scan the string, pulling off each address piece
			*/
			int i = 0;
			char * p = strtok(netbuf,".");
			int x;
			while (p) {
				sscanf(p,"%x",&x);			// convert from hex string to int
				if (i < 4) {
					net[i] = (char)x;			// fill NetNum
				} else {
					node[i-4] = (char)x;		// fill NetNode
				}
				i++;
				p = strtok(NULL,".");
			}

			/*
			** If all the address components were successfully read, fill in the
			** BridgeNet with a broadcast address to the network across the bridge.
			*/
			if (i >= 4) {
				IsBridge = 1;
				memset(node, 0xff, 6);
				BridgeNet = IPXAddressClass(net, node);
			}
		}

	}

	delete [] buffer;
}

/***********************************************************************************************
 * Save_Original_Palette -- Save the current palette for later restoration                     *
 *                                                                                             *
 * Reads directly from hardware palette registers at $FF8240-$FF825E                          *
 *=============================================================================================*/
static void Save_Original_Palette(void)
{
	if (!OriginalPaletteSaved) {
		// Read directly from hardware palette registers
		for (int i = 0; i < PALETTE_REG_COUNT; i++) {
			SavedOriginalPalette[i] = PaletteRegs[i];
		}
		OriginalPaletteSaved = true;
	}
}

/***********************************************************************************************
 * Restore_Original_Palette -- Restore the original palette                                   *
 *                                                                                             *
 * Writes directly to hardware palette registers at $FF8240-$FF825E                           *
 *=============================================================================================*/
static void Restore_Original_Palette(void)
{
	if (OriginalPaletteSaved) {
		// Write directly to hardware palette registers
		for (int i = 0; i < PALETTE_REG_COUNT; i++) {
			PaletteRegs[i] = SavedOriginalPalette[i];
		}
		OriginalPaletteSaved = false;
	}
}

/***********************************************************************************************
 * Init_Temperat_HW_Palette -- Load first 16 colors of TEMPERAT.PAL into ST hardware palette *
 *                                                                                             *
 * C&C .PAL uses 6-bit RGB (0-63) per channel; STE registers use the same packing as the       *
 * previous greyscale init (nibble split per gun, then R in bits 11-8, G in 7-4, B in 3-0).   *
 *=============================================================================================*/
static void Init_Temperat_HW_Palette(void)
{
	St_HW_Palette_Write_Temperat_First16(PaletteRegs);
	
	// Draw an 8-pixel high bar containing all 16 colors in the vertical middle of the screen
	// The bar fills the screen horizontally
	unsigned char *screen = (unsigned char *)Physbase();
	if (screen) {
		// LoRes mode: 320x200, 16 colors (4 bitplanes)
		// Memory layout: word-interleaved bitplanes
		// For each group of 16 pixels: 4 words (one per bitplane), each word is 2 bytes
		// So 16 pixels = 8 bytes (4 words × 2 bytes)
		// Each scan line: 320 pixels / 16 = 20 groups × 8 bytes = 160 bytes per line
		const int screen_width = 320;
		const int screen_height = 200;
		const int bytes_per_line = 160;  // 20 groups × 8 bytes per group
		const int pixels_per_group = 16;  // 16 pixels per group
		const int bytes_per_group = 8;   // 4 words × 2 bytes per word
		const int bar_height = 8;
		const int bar_y = (screen_height - bar_height) / 2;  // Vertical middle
		const int pixels_per_color = screen_width / PALETTE_REG_COUNT;  // 20 pixels per color
		
		// Draw the bar: 8 lines high, each color taking pixels_per_color pixels horizontally
		for (int y = 0; y < bar_height; y++) {
			int line_y = bar_y + y;
			unsigned char *line_base = screen + (line_y * bytes_per_line);
			
			for (int color = 0; color < PALETTE_REG_COUNT; color++) {
				// Each color occupies pixels_per_color pixels
				for (int px = 0; px < pixels_per_color; px++) {
					int x = color * pixels_per_color + px;
					int group_index = x / pixels_per_group;  // Which group of 16 pixels (0-19)
					int bit_in_group = x % pixels_per_group;  // Which bit within the group (0-15)
					int bit_in_word = 15 - bit_in_group;  // Bit position in word (MSB = leftmost pixel)
					
					// Calculate base address for this group
					unsigned char *group_base = line_base + (group_index * bytes_per_group);
					
					// Each bitplane is a word (2 bytes) at offset: plane * 2
					unsigned short *bp0_word = (unsigned short *)(group_base + 0 * 2);  // Bitplane 0 (LSB)
					unsigned short *bp1_word = (unsigned short *)(group_base + 1 * 2);  // Bitplane 1
					unsigned short *bp2_word = (unsigned short *)(group_base + 2 * 2);  // Bitplane 2
					unsigned short *bp3_word = (unsigned short *)(group_base + 3 * 2);  // Bitplane 3 (MSB)
					
					// Set the bit in each bitplane based on the color value
					unsigned short bit_mask = 1 << bit_in_word;
					if (color & 0x01) *bp0_word |= bit_mask; else *bp0_word &= ~bit_mask;  // Bitplane 0
					if (color & 0x02) *bp1_word |= bit_mask; else *bp1_word &= ~bit_mask;  // Bitplane 1
					if (color & 0x04) *bp2_word |= bit_mask; else *bp2_word &= ~bit_mask;  // Bitplane 2
					if (color & 0x08) *bp3_word |= bit_mask; else *bp3_word &= ~bit_mask;  // Bitplane 3
				}
			}
		}
	}
}

/***********************************************************************************************
 * Switch_To_LoRes -- Switch to low resolution mode if possible                              *
 *                                                                                             *
 * Attempts to switch to LoRes (320x200) mode. Saves original resolution for restoration.    *
 *=============================================================================================*/
static void Switch_To_LoRes(void)
{
	// Get current resolution
	int current_rez = Getrez();
	
	// Save original resolution if not already saved
	if (OriginalResolution == -1) {
		OriginalResolution = current_rez;
	}
	
	// If already in LoRes, nothing to do
	if (current_rez == 0) {
		return;
	}
	
	// Try to switch to LoRes (resolution 0)
	// Setscreen parameters: lscrn=-1 (keep current logical), pscrn=-1 (keep current physical), rez=0 (LoRes)
	Setscreen(-1L, -1L, 0);
	
	// Verify the switch was successful
	int new_rez = Getrez();
	if (new_rez == 0) {
		ResolutionChanged = true;
		printf("C&C - Switched to LoRes mode (320x200).\n");
	} else {
		printf("C&C - Warning: Could not switch to LoRes mode. Current mode: %d\n", new_rez);
	}
}

/***********************************************************************************************
 * Restore_Original_Resolution -- Restore the original screen resolution                      *
 *=============================================================================================*/
static void Restore_Original_Resolution(void)
{
	if (ResolutionChanged && OriginalResolution != -1) {
		// Restore original resolution
		Setscreen(-1L, -1L, OriginalResolution);
		ResolutionChanged = false;
		printf("C&C - Restored original resolution mode: %d\n", OriginalResolution);
	}
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
	(void)hwnd; (void)w; (void)h; (void)bits_per_pixel;
	
	// Try to switch to LoRes mode if not already in it
	Switch_To_LoRes();
	
	// Check that we're in Lorez (low resolution) mode
	int rez = Getrez();
	if (rez != 0) {
		printf("C&C - Error: Not in Lorez (low resolution) mode. Current mode: %d\n", rez);
		printf("C&C - Please switch to low resolution (320x200) mode.\n");
		return FALSE;
	}
	
	// Save original palette before we modify it
	Save_Original_Palette();
	
	/* First 16 entries of temperate theater palette -> ST hardware (replaces grey ramp). */
	Init_Temperat_HW_Palette();

	/* Hide GEM/VDI hardware mouse; game uses WWMouseClass software cursor on SeenBuff. */
	Cursconf(CURS_HIDE, 0);
	
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

