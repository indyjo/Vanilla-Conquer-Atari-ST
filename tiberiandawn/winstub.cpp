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
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : WINSTUB.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Steve Tall                                                   *
 *                                                                                             *
 *                   Start Date : 10/04/95                                                     *
 *                                                                                             *
 *                  Last Update : October 4th 1995 [ST]                                        *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Overview:                                                                                   *
 *   This file contains stubs for undefined externals when linked under Watcom for Win 95      *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 *                                                                                             *
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "function.h"
#include "externs.h"
#include "common/wsproto.h"
#include "common/vqaaudio.h"

#ifdef POSIX
#include "atarilib/c2p.h"
#include "atarilib/drawbuff.h"
#include "atarilib/st_bftp_sprite_cache.h"
#include <cstdarg>
#include <cstdint>
#endif

void output(short, short)
{
}

bool InDebugger = false;
int ReadyToQuit = 0;

#ifdef _WIN32
unsigned int CCFocusMessage = WM_USER + 50; // Private message for receiving application focus
#endif

ThemeType OldTheme = THEME_NONE;

/***********************************************************************************************
 * Focus_Loss -- this function is called when a library function detects focus loss            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/1/96 2:10PM ST : Created                                                               *
 *=============================================================================================*/

void Focus_Loss(void)
{
#ifdef SDL_BUILD
    GameInFocus = false;
    Theme.Suspend();
    VQA_PauseAudio();
#else
    if (SoundOn) {
        if (OldTheme == THEME_NONE) {
            OldTheme = Theme.What_Is_Playing();
        }
    }
    Theme.Stop();
#endif
    Stop_Primary_Sound_Buffer();
}

void Focus_Restore(void)
{
#ifdef SDL_BUILD
    GameInFocus = true;
    VQA_ResumeAudio();
#endif
    Map.Flag_To_Redraw(true);
    Start_Primary_Sound_Buffer(true);

#ifndef SDL_BUILD
    VisiblePage.Clear();
    HiddenPage.Clear();
#endif
}

/***********************************************************************************************
 * Check_For_Focus_Loss -- check for the end of the focus loss                                 *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/2/96 10:49AM ST : Created                                                              *
 *=============================================================================================*/

void Check_For_Focus_Loss(void)
{
#if defined(SDL_BUILD)
    Keyboard->Check();
#elif !defined(REMASTER_BUILD) && defined(_WIN32)
    static BOOL focus_last_time = 1;
    MSG msg;

    if (!GameInFocus) {
        Focus_Loss();
        while (PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE | PM_NOYIELD)) {
            if (!GetMessageA(&msg, NULL, 0, 0)) {
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    if (!focus_last_time && GameInFocus) {

        VQA_PauseAudio();
        CountDownTimerClass cd;
        cd.Set(60 * 1);

        do {
            while (PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE)) {
                if (!GetMessageA(&msg, NULL, 0, 0)) {
                    return;
                }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }

        } while (cd.Time());
        VQA_ResumeAudio();
        // AllSurfaces.Restore_Surfaces();
        // VisiblePage.Clear();
        // HiddenPage.Clear();
        // Map.Flag_To_Redraw(true);
        PostMessageA(MainWindow, CCFocusMessage, 0, 0);
    }

    focus_last_time = GameInFocus;
#endif
}

extern bool InMovie;
#if !defined(REMASTER_BUILD) && defined(_WIN32) && !defined(SDL_BUILD)
long FAR PASCAL Windows_Procedure(HWND hwnd, UINT message, UINT wParam, LONG lParam)
{

    int low_param = LOWORD(wParam);

    if (message == CCFocusMessage) {
        Start_Primary_Sound_Buffer(TRUE);
        if (!InMovie) {
            Theme.Queue_Song(OldTheme);
            OldTheme = THEME_NONE;
        }
        return (0);
    }

    /*
    **	Pass this message through to the keyboard handler. If the message
    **	was processed and requires no further action, then return with
    **	this information.
    */
    if (Keyboard->Message_Handler(hwnd, message, wParam, lParam)) {
        return (1);
    }

    switch (message) {

    case WM_DESTROY:
        CCDebugString("C&C95 - WM_DESTROY message received.\n");
        CCDebugString("C&C95 - About to call Prog_End.\n");
        Prog_End();
        CCDebugString("C&C95 - About to release the video surfaces.\n");
        VisiblePage.Un_Init();
        HiddenPage.Un_Init();
        AllSurfaces.Release();
        if (!InDebugger) {
            CCDebugString("C&C95 - About to reset the video mode.\n");
            Reset_Video_Mode();
        }
        CCDebugString("C&C95 - Posting the quit message.\n");
        PostQuitMessage(0);
        /*
        ** If we are shutting down gracefully than flag that the message loop has finished.
        ** If this is a forced shutdown (ReadyToQuit == 0) then try and close down everything
        ** before we exit.
        */
        if (ReadyToQuit) {
            CCDebugString("C&C95 - We are now ready to quit.\n");
            ReadyToQuit = 2;
        } else {
            CCDebugString("C&C95 - Emergency shutdown.\n");
#ifdef NETWORKING
            CCDebugString("C&C95 - Shut down the network stuff.\n");
#ifndef DEMO
            Shutdown_Network();
#endif
            CCDebugString("C&C95 - Kill the Winsock stuff.\n");
            if (Winsock.Get_Connected())
                Winsock.Close();
#endif // NETWORKING
            CCDebugString("C&C95 - Call ExitProcess.\n");
            ExitProcess(0);
        }
        CCDebugString("C&C95 - Clean & ready to quit.\n");
        return (0);

    case WM_ACTIVATEAPP:
        GameInFocus = (BOOL)wParam;
        if (!GameInFocus) {
            Focus_Loss();
        }
        AllSurfaces.Set_Surface_Focus(GameInFocus);
        AllSurfaces.Restore_Surfaces();
        //			if (GameInFocus){
        //				Restore_Cached_Icons();
        //				Map.Flag_To_Redraw(true);
        //				Start_Primary_Sound_Buffer(TRUE);
        //				if (WWMouse) WWMouse->Set_Cursor_Clip();
        //			}
        return (0);
#if (0)
    case WM_ACTIVATE:
        if (low_param == WA_INACTIVE) {
            GameInFocus = FALSE;
            Focus_Loss();
        }
        return (0);
#endif //(0)

    case WM_SYSCOMMAND:
        switch (wParam) {

        case SC_CLOSE:
            /*
            ** Windows sent us a close message. Probably in response to Alt-F4. Ignore it by
            ** pretending to handle the message and returning 0;
            */
            return (0);

        case SC_SCREENSAVE:
            /*
            ** Windoze is about to start the screen saver. If we just return without passing
            ** this message to DefWindowProc then the screen saver will not be allowed to start.
            */
            return (0);
        }
        break;

#ifdef FORCE_WINSOCK
    case WM_ACCEPT:
    case WM_HOSTBYADDRESS:
    case WM_HOSTBYNAME:
    case WM_ASYNCEVENT:
    case WM_UDPASYNCEVENT:
        Winsock.Message_Handler(hwnd, message, wParam, lParam);
        return (0);
#endif // FORCE_WINSOCK
    }

    return (DefWindowProc(hwnd, message, wParam, lParam));
}

#endif

/***********************************************************************************************
 * Create_Main_Window -- opens the MainWindow for C&C                                          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    instance -- handle to program instance                                            *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    10/10/95 4:08PM ST : Created                                                             *
 *=============================================================================================*/

#define CC_ICON 1

#if defined(_WIN32) && !defined(SDL_BUILD)
void Create_Main_Window(HANDLE instance, int width, int height)

{
#ifdef REMASTER_BUILD
    MainWindow = NULL;
    return;
#else
    HWND hwnd;
    WNDCLASSA wndclass;

    STARTUPINFOA sinfo;
    int command_show;

    sinfo.dwFlags = 0;
    GetStartupInfoA(&sinfo);

    if (sinfo.dwFlags & STARTF_USESHOWWINDOW) {
        command_show = sinfo.wShowWindow;
    } else {
        command_show = SW_SHOWDEFAULT;
    }

    //
    // Register the window class
    //

    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = Windows_Procedure;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = (HINSTANCE)instance;
    wndclass.hIcon = LoadIconA((HINSTANCE)instance, MAKEINTRESOURCEA(CC_ICON));
    wndclass.hCursor = NULL;
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = "Command & Conquer"; // NULL
    wndclass.lpszClassName = "Command & Conquer";

    RegisterClassA(&wndclass);

    //
    // Create our main window
    //
    hwnd = CreateWindowExA(WS_EX_TOPMOST,
                           "Command & Conquer",
                           "Command & Conquer",
                           WS_POPUP | WS_MAXIMIZE,
                           0,
                           0,
                           width,
                           height,
                           NULL,
                           NULL,
                           (HINSTANCE)instance,
                           NULL);

    ShowWindow(hwnd, command_show);
    ShowCommand = command_show;
    UpdateWindow(hwnd);
    SetFocus(hwnd);
    MainWindow = hwnd; // Save the handle to our main window

    CCFocusMessage = RegisterWindowMessageA("CC_GOT_FOCUS");

    Audio_Focus_Loss_Function = &Focus_Loss;
    Misc_Focus_Loss_Function = &Focus_Loss;
    Misc_Focus_Restore_Function = &Focus_Restore;
    Gbuffer_Focus_Loss_Function = &Focus_Loss;
#endif
}
#endif

typedef struct tColourList
{

    char Red;
    char Green;
    char Blue;
} ColourList;

ColourList ColourLookup[9] = {0,  0,  0,  63, 0, 0,  0,  63, 0,  0,  0,  63, 63, 0,
                              63, 63, 63, 0,  0, 63, 63, 32, 32, 32, 63, 63, 63};

int DebugColour = 1;

void Set_Palette_Register(int number, int red, int green, int blue);
//#pragma off (unreferenced)
void Colour_Debug(int call_number)
{
    //#if 0
    // if (DebugColour==call_number || !call_number){

    // if (call_number){
    //	Wait_Vert_Blank();
    //}

    // ST - 1/3/2019 10:43AM
    // Set_Palette_Register (0,ColourLookup[call_number].Red ,
    //								ColourLookup[call_number].Green,
    //								ColourLookup[call_number].Blue);
    //}
    //#endif
}

//#pragma on (unreferenced)

bool Any_Locked()
{
    if (SeenBuff.Get_LockCount() || HidPage.Get_LockCount()) {
        return true;
    } else {
        return false;
    }
}

#ifdef POSIX
/*
** Atari ST: KEYFBUFF.ASM is not linked for m68k; provide Buffer_Frame_To_Page here.
*/
extern "C" void Bftp_ExArgs_init_zero(Bftp_ExArgs* ex)
{
    if (!ex) {
        return;
    }
    ex->ghost_table = nullptr;
    ex->fade_table = nullptr;
    ex->fading_num = 0;
    ex->predoffset = 0;
    ex->identity_key = 0L;
    ex->lazy_frame_fill = nullptr;
    ex->lazy_frame_ctx = nullptr;
    ex->lru_scratch_root = nullptr;
}

long Buffer_Frame_To_Page_Ex(int x,
                             int y,
                             int w,
                             int h,
                             void* Buffer,
                             GraphicViewPortClass& view,
                             int flags,
                             Bftp_ExArgs const* ex_in)
{
    static Bftp_ExArgs const s_bftp_ex_empty = {0};
    Bftp_ExArgs const* const ex = (ex_in != nullptr) ? ex_in : &s_bftp_ex_empty;

    const uint8_t* ghost_table = (const uint8_t*)ex->ghost_table;
    const uint8_t* fade_table = (const uint8_t*)ex->fade_table;
    (void)ex->fading_num;
    (void)ex->predoffset;

    const int trans = (flags & 0x40) ? 1 : 0;
    const int centered = (flags & 0x20) ? 1 : 0;
    const int predator = (flags & 0x0200) ? 1 : 0;

    if (w <= 0 || h <= 0) {
        return 0;
    }
    if (!Buffer && ex->lazy_frame_fill == nullptr) {
        return 0;
    }

    int draw_x = x;
    int draw_y = y;
    if (centered) {
        draw_x -= (w >> 1);
        draw_y -= (h >> 1);
    }

    GraphicBufferClass* gb = view.Get_Graphic_Buffer();
    const int vpw = view.Get_Width();
    const int vph = view.Get_Height();
    int src_x = 0;
    int src_y = 0;
    int dst_x = draw_x;
    int dst_y = draw_y;
    int blit_w = w;
    int blit_h = h;

    if (dst_x < 0) {
        src_x = -dst_x;
        blit_w -= src_x;
        dst_x = 0;
    }
    if (dst_y < 0) {
        src_y = -dst_y;
        blit_h -= src_y;
        dst_y = 0;
    }
    if (dst_x + blit_w > vpw) {
        blit_w = vpw - dst_x;
    }
    if (dst_y + blit_h > vph) {
        blit_h = vph - dst_y;
    }
    if (blit_w <= 0 || blit_h <= 0) {
        return 0;
    }

    bool const planar_bftp_route = gb && gb->Is_ST_Planar();
    bool const planar_decode_on_miss = planar_bftp_route && ex->lazy_frame_fill != nullptr && ex->lru_scratch_root != nullptr;

    void* raster_base = Buffer;
    if (ex->lazy_frame_fill != nullptr && !planar_decode_on_miss) {
        unsigned long const built = (*ex->lazy_frame_fill)(ex->lazy_frame_ctx);
        if (built == 0UL) {
            return 0;
        }
        raster_base = (void*)(uintptr_t)built;
    }
    if (planar_decode_on_miss) {
        raster_base = (void*)ex->lru_scratch_root;
    }

    const uint8_t* src_raster = (const uint8_t*)raster_base + static_cast<size_t>(src_y) * static_cast<size_t>(w)
                                + static_cast<size_t>(src_x);

    if (planar_bftp_route) {
        uint8_t* root = (uint8_t*)gb->Get_Buffer();
        const int ax0 = view.Get_XPos() + dst_x;
        const int ay0 = view.Get_YPos() + dst_y;
        (void)predator;
        Bftp_Lazy_Frame_FillFn lazy_miss_fn = planar_decode_on_miss ? ex->lazy_frame_fill : nullptr;
        void* lazy_miss_ctx = planar_decode_on_miss ? ex->lazy_frame_ctx : nullptr;
        const long drew = ST_BFTP_Buffer_Frame_Planar_Composite(root,
                                                              ax0,
                                                              ay0,
                                                              src_raster,
                                                              blit_w,
                                                              blit_h,
                                                              w,
                                                              trans,
                                                              ghost_table,
                                                              fade_table,
                                                              (const uint8_t*)raster_base,
                                                              src_x,
                                                              src_y,
                                                              w,
                                                              h,
                                                              ex->identity_key,
                                                              (unsigned long (*)(void*))lazy_miss_fn,
                                                              lazy_miss_ctx);
        if (drew > 0) {
            return drew;
        }
        /*
         * Sprite cache tiers top out at 96×96; larger keyframe canvases (e.g. OPTIONS.SHP
         * dialog chrome used for menu rivets) get tier_dim 0 and skip the blitter entirely.
         * Decode on demand and use the pre-LRU C2P path, or per-pixel remap when ghost/fade.
         */
        if (planar_decode_on_miss && ex->lazy_frame_fill != nullptr && ex->lru_scratch_root != nullptr) {
            unsigned long const built = (*ex->lazy_frame_fill)(ex->lazy_frame_ctx);
            if (built == 0UL
                || (const uint8_t*)(uintptr_t)built != (const uint8_t*)ex->lru_scratch_root) {
                return 0;
            }
            raster_base = (void*)ex->lru_scratch_root;
            src_raster = (const uint8_t*)raster_base + static_cast<size_t>(src_y) * static_cast<size_t>(w)
                         + static_cast<size_t>(src_x);
        }
        if (ghost_table == nullptr && fade_table == nullptr) {
            C2P_Blit_Linear8_To_Planar(root, ax0, ay0, src_raster, blit_w, blit_h, w, trans);
            return static_cast<long>(static_cast<size_t>(blit_w) * static_cast<size_t>(blit_h));
        }
    }

    for (int row = 0; row < blit_h; ++row) {
        const uint8_t* srow = src_raster + static_cast<size_t>(row) * static_cast<size_t>(w);
        for (int col = 0; col < blit_w; ++col) {
            const uint8_t s_raw = srow[col];
            if (trans && s_raw == 0) {
                continue;
            }
            uint8_t out;
            if (ghost_table) {
                const uint8_t it = ghost_table[s_raw];
                if (it != 0xFFu) {
                    uint8_t d = static_cast<uint8_t>(view.Get_Pixel(dst_x + col, dst_y + row));
                    out = ghost_table[256 + (static_cast<size_t>(it) << 8) + static_cast<size_t>(d)];
                } else {
                    out = fade_table ? fade_table[s_raw] : s_raw;
                }
            } else {
                out = fade_table ? fade_table[s_raw] : s_raw;
            }
            view.Put_Pixel(dst_x + col, dst_y + row, out);
        }
    }
    return static_cast<long>(blit_w * blit_h);
}

long Buffer_Frame_To_Page(int x, int y, int w, int h, void* Buffer, GraphicViewPortClass& view, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    const int ghost = (flags & 0x1000) ? 1 : 0;
    const int fading = (flags & 0x0100) ? 1 : 0;
    const int predator = (flags & 0x0200) ? 1 : 0;

    Bftp_ExArgs ex = {0};

    if (ghost && fading) {
        ex.ghost_table = (const unsigned char*)va_arg(ap, void*);
        ex.fade_table = (const unsigned char*)va_arg(ap, void*);
        ex.fading_num = va_arg(ap, int);
        ex.predoffset = va_arg(ap, int);
    } else if (fading) {
        ex.fade_table = (const unsigned char*)va_arg(ap, void*);
        ex.fading_num = va_arg(ap, int);
        ex.predoffset = va_arg(ap, int);
    } else if (predator) {
        ex.predoffset = va_arg(ap, int);
    } else if (ghost) {
        ex.ghost_table = (const unsigned char*)va_arg(ap, void*);
        ex.predoffset = va_arg(ap, int);
    }
    va_end(ap);

    return Buffer_Frame_To_Page_Ex(x, y, w, h, Buffer, view, flags, &ex);
}
#endif

/***********************************************************************************************
 * Memory_Error_Handler -- Handle a possibly fatal failure to allocate memory                  *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    5/22/96 3:57PM ST : Created                                                              *
 *=============================================================================================*/
void Memory_Error_Handler(void)
{
    GlyphX_Debug_Print("Error - out of memory.");
    VisiblePage.Clear();
    Set_Palette(GamePalette);
    while (Get_Mouse_State()) {
        Show_Mouse();
    };
    WWMessageBox().Process("Error - out of memory.", "Abort", nullptr, nullptr, false);

    // Nope. ST - 1/10/2019 10:38AM
    // PostQuitMessage( 0 );
    // ExitProcess(0);
}
