/*
 * windows_time.cpp - Windows time function implementations for Atari ST/MiNT
 */

#include "windows.h"
#include <time.h>

/* GetSystemTimeAsFileTime - Get system time as FILETIME */
void GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
	if (!lpSystemTimeAsFileTime) {
		return;
	}
	
	// Get current time in seconds since epoch
	time_t now = time(NULL);
	
	// Convert to Windows FILETIME (100-nanosecond intervals since January 1, 1601)
	// FILETIME epoch: January 1, 1601 00:00:00 UTC
	// Unix epoch: January 1, 1970 00:00:00 UTC
	// Difference: 11644473600 seconds = 0x019DB1DED53E8000 in 100-nanosecond intervals
	
	const unsigned long long EPOCH_DIFF = 11644473600ULL;
	const unsigned long long HUNDRED_NANOS_PER_SEC = 10000000ULL;
	
	unsigned long long filetime = ((unsigned long long)now + EPOCH_DIFF) * HUNDRED_NANOS_PER_SEC;
	
	lpSystemTimeAsFileTime->dwLowDateTime = (DWORD)(filetime & 0xFFFFFFFF);
	lpSystemTimeAsFileTime->dwHighDateTime = (DWORD)((filetime >> 32) & 0xFFFFFFFF);
}

