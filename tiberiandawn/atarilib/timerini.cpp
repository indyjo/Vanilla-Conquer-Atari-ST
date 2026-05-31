/*
 * timerini.cpp - Timer initialization for Atari ST/MiNT
 * 
 * This file provides the global TickCount timer instance
 */

#include "timer.h"
#include "tcpip.h"
#include "ccdde.h"
#include "windows.h" // For BOOL

// TickCount / CountDown come from ../common/timer.cpp on ATARI_ST.
TcpipManagerClass Winsock;
extern bool Server;
// PlanetWestwoodIPAddress, PlanetWestwoodPortNumber, PlanetWestwoodIsHost, UseVirtualSubnetServer, InternetMaxPlayers
// are defined in INTERNET.CPP, so they should not be defined here.
// char PlanetWestwoodIPAddress[40] = "";
// long PlanetWestwoodPortNumber = 0;
// bool PlanetWestwoodIsHost = false;
// bool UseVirtualSubnetServer = false;
// int InternetMaxPlayers = 0;

// Global DDE server instance
DDEServerClass DDEServer;

