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


/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D    S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer - Red Alert                                *
 *                                                                                             *
 *                    File Name : CCDDE.H                                                      *
 *                                                                                             *
 *                   Programmer : Steve Tall                                                   *
 *                                                                                             *
 *                   Start Date : 10/04/95                                                     *
 *                                                                                             *
 *                  Last Update : August 5th, 1996 [ST]                                        *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Overview:                                                                                   *
 *   C&C's interface to the DDE class - Atari ST stub version                                  *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 *                                                                                             *
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

// DDE is Windows-specific, provide stub for Atari ST

#include "windows.h" // For BOOL
#include <stddef.h>  // For NULL

class DDEServerClass {
public:
	/*
	** Enumeration for DDE packet types from WChat
	*/
	enum {
		DDE_PACKET_START_MPLAYER_GAME,		//Start game packet. This includes game options
		DDE_PACKET_GAME_RESULTS,				//Game results packet. The game statistics.
		DDE_PACKET_HEART_BEAT,					//Heart beat packet so we know WChat is still there.
		DDE_TICKLE,									//Message to prompt other app to take focus.
		DDE_CONNECTION_FAILED
	};

	DDEServerClass(void) {}
	~DDEServerClass(void) {}
	char *Get_MPlayer_Game_Info(void) { return NULL; }
	int Get_MPlayer_Game_Info_Length() { return 0; }
	BOOL Callback(unsigned char *data, long length) { return FALSE; }
	void Delete_MPlayer_Game_Info(void) {}
	void Enable(void) {}
	void Disable(void) {}
	int Time_Since_Heartbeat(void) { return 0; }
};

extern DDEServerClass DDEServer;
extern BOOL Send_Data_To_DDE_Server(char *data, int length, int packet_type);

