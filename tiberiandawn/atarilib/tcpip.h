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


/***************************************************************************
 **   C O N F I D E N T I A L --- W E S T W O O D    S T U D I O S        **
 ***************************************************************************
 *                                                                         *
 *                 Project Name : Command & Conquer                        *
 *                                                                         *
 *                    File Name : TCPIP.CPP                                *
 *                                                                         *
 *                   Programmer : Steve Tall                               *
 *                                                                         *
 *                   Start Date : March 11th, 1996                         *
 *                                                                         *
 *                  Last Update : March 11th, 1996 [ST]                    *
 *                                                                         *
 *-------------------------------------------------------------------------*
 *                                                                         *
 *                                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*
 * Functions:                                                              *
 *                                                                         *
 *                                                                         *
 *                                                                         *
 *                                                                         *
 *                                                                         *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

// TCP/IP networking disabled for Atari ST - stub declarations only

#include "windows.h" // For BOOL

// Constants matching WIN32LIB/tcpip.h
#define PLANET_WESTWOOD_PASSWORD_MAX 20
#define IP_ADDRESS_MAX 40
#define PORT_NUMBER_MAX 6

extern bool Server;

// Stub class for TcpipManagerClass
class TcpipManagerClass {
public:
	TcpipManagerClass(void) {}
	~TcpipManagerClass(void) {}
	BOOL Init(void) { return FALSE; }
	void Start_Server(void) {}
	void Start_Client(void) {}
	void Close_Socket(void* s) {}
	void Message_Handler(void* window, unsigned int message, unsigned int wParam, long lParam) {}
	void Copy_To_In_Buffer(int bytes) {}
	int  Read(void *buffer, int buffer_len) { return 0; }
	void Write(void *buffer, int buffer_len) {}
	BOOL Add_Client(void) { return FALSE; }
	void Close(void) {}
	void Set_Host_Address(char *address) {}
	void Set_Protocol_UDP(BOOL state) {}
	void Clear_Socket_Error(void* socket) {}
	BOOL Get_Connected(void) { return FALSE; }
	enum ConnectStatusEnum {
		CONNECTED_OK = 0,
		NOT_CONNECTING,
		CONNECTING,
		UNABLE_TO_CONNECT_TO_SERVER,
		CONTACTING_SERVER,
		SERVER_ADDRESS_LOOKUP_FAILED,
		RESOLVING_HOST_ADDRESS,
		UNABLE_TO_ACCEPT_CLIENT,
		UNABLE_TO_CONNECT,
		CONNECTION_LOST
	};
	ConnectStatusEnum Get_Connection_Status(void) { return NOT_CONNECTING; }
};

extern TcpipManagerClass Winsock;
extern char PlanetWestwoodIPAddress[40];
extern long PlanetWestwoodPortNumber;
extern bool PlanetWestwoodIsHost;
extern int  Read_Game_Options(char *);
extern bool UseVirtualSubnetServer;
extern int  InternetMaxPlayers;

