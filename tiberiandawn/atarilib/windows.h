/*
 * windows.h - Windows API compatibility header for Atari ST/MiNT
 * 
 * This header provides only the Windows API definitions actually used
 * in the main source code (excluding WIN32LIB which won't be compiled).
 * 
 * Functions are declared but need to be implemented in ATARILIB modules.
 */

#ifndef WINDOWS_H
#define WINDOWS_H

// POSIX string functions for Windows compatibility
#ifdef POSIX
#include <strings.h>  // For strcasecmp
#include <stddef.h>   // For size_t
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Basic Types (actually used in main source)
 *============================================================================*/

#ifndef _BASETSD_H
typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef unsigned long      DWORD;
typedef unsigned long      ULONG;  // Windows unsigned long type
typedef long long          __int64;  // 64-bit integer type
typedef unsigned long long uint64;   // 64-bit unsigned integer type
typedef unsigned int       UINT;
typedef int                BOOL;
typedef long               LONG;
typedef void*              HANDLE;
typedef HANDLE             HWND;
typedef HANDLE             HINSTANCE;
typedef const char*        LPCSTR;
typedef char*              LPSTR;
typedef const char*        LPCTSTR;
typedef void*              LPVOID;
typedef const void*        LPCVOID;
typedef DWORD*             LPDWORD;

/* Windows FILETIME structure */
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

/* Windows time functions */
void GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
#endif

/*=============================================================================
 * Calling Conventions (actually used)
 *============================================================================*/

#ifndef WINAPI
#define WINAPI __stdcall
#endif

#ifndef __stdcall
#define __stdcall
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef far
#define far
#endif

#ifndef FAR
#define FAR
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef __declspec
#define __declspec(x)
#endif

#ifndef PASCAL
#define PASCAL __stdcall
#endif

/*=============================================================================
 * Constants (actually used in main source)
 *============================================================================*/

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

/* DLL Entry Point Reasons - used in startup.cpp */
#define DLL_PROCESS_ATTACH    1
#define DLL_PROCESS_DETACH    0
#define DLL_THREAD_ATTACH     2
#define DLL_THREAD_DETACH     3

/* ShowWindow Commands - used in startup.cpp, netdlg.cpp, internet.cpp */
#define SW_HIDE               0
#define SW_SHOWNORMAL         1
#define SW_NORMAL             1
#define SW_SHOWMINIMIZED      2
#define SW_SHOWMAXIMIZED      3
#define SW_MAXIMIZE           3
#define SW_MINIMIZE           6
#define SW_RESTORE            9

/* Window Messages - WM_USER used in tcpip.h, WM_MOUSEMOVE in winstub.cpp (commented) */
#define WM_USER               0x0400
#define WM_MOUSEMOVE          0x0200

/* Handle Values - used in winstub.cpp, stats.cpp */
#define INVALID_HANDLE_VALUE ((HANDLE)(-1))

/* File path constants - used in externs.h */
#define MAX_PATH 260
#define _MAX_PATH 260
#define _MAX_FNAME 256
#define _MAX_EXT 256

/* Registry Keys - used in internet.cpp */
#define HKEY_LOCAL_MACHINE    ((HKEY)(ULONG_PTR)((LONG)0x80000002))
typedef HANDLE HKEY;

/* Registry Error Codes - used in internet.cpp */
#define ERROR_SUCCESS         0L

/*=============================================================================
 * Macros (actually used)
 *============================================================================*/

#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define ULONG_PTR unsigned long
#define DWORD_PTR unsigned long

/*=============================================================================
 * Structures (actually used)
 *============================================================================*/

/* Message parameter types - define before using */
#ifndef UINT_PTR
#define UINT_PTR unsigned long
#endif
#ifndef LONG_PTR
#define LONG_PTR long
#endif

typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;

/* Function pointer types - used in winstub.cpp and function.h */
typedef LRESULT (WINAPI *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT (WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

/* Handle types - used in winstub.cpp (commented out but types needed) */
typedef HANDLE HICON;
typedef HANDLE HCURSOR;
typedef HANDLE HBRUSH;

/* Used in winstub.cpp (commented out but types needed) */
typedef struct tagWNDCLASS {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCTSTR lpszMenuName;
    LPCTSTR lpszClassName;
} WNDCLASS, *PWNDCLASS, *LPWNDCLASS;

/*=============================================================================
 * Function Declarations (actually used in main source)
 *============================================================================*/

/* Window Management - used in startup.cpp, netdlg.cpp, internet.cpp */
HWND WINAPI FindWindow(LPCSTR lpClassName, LPCSTR lpWindowName);
BOOL WINAPI SetForegroundWindow(HWND hWnd);
BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow);

/* DLL Entry Point - used in startup.cpp */
BOOL WINAPI DllMain(HINSTANCE instance, unsigned int fdwReason, void *lpvReserved);

/* Registry Functions - used in internet.cpp */
typedef BYTE* LPBYTE;
LONG WINAPI RegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);

/* String Functions - Windows compatibility */
#ifdef POSIX
// _stricmp is Windows-specific, use POSIX strcasecmp instead
#define _stricmp strcasecmp

// memicmp is Windows-specific, provide portable implementation
// Case-insensitive memory comparison
static inline int memicmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	
	for (size_t i = 0; i < n; i++) {
		unsigned char c1 = p1[i];
		unsigned char c2 = p2[i];
		
		// Convert to lowercase for comparison
		if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
		if (c2 >= 'A' && c2 <= 'Z') c2 += ('a' - 'A');
		
		if (c1 != c2) {
			return (c1 < c2) ? -1 : 1;
		}
	}
	return 0;
}
#endif

/* Note: Get_Registry_Sub_Key appears to be a custom wrapper function,
 * not a standard Windows API, so it's not declared here.
 * It should be implemented separately in ATARILIB.
 */

#ifdef __cplusplus
}
#endif

#endif /* WINDOWS_H */
