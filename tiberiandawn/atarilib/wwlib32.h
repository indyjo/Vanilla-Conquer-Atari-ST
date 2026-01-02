/*
 * wwlib32.h - Westwood 32-bit library header for Atari ST/MiNT
 * 
 * This header includes all the necessary headers for the library.
 * It should be included before jshell.h to provide function declarations.
 */

#ifndef WWLIB32_H
#define WWLIB32_H

// Include wwstd.h first for color constants (from COMMONLIB)
#include "../COMMONLIB/wwstd.h"

// Include memflag.h early for Mem_Copy and Add_Long_To_Pointer
// (needed even when WWMEM_H is defined in function.h)
#include "memflag.h"

// Include gbuffer.h early for GraphicBufferClass definitions
#include "gbuffer.h"

// Include coordinate functions (from COMMONLIB)
#include "../COMMONLIB/coorda.h"

// Include keyboard and mouse headers for function declarations
#include "keyboard.h"
#include "mouse.h"

// Stub headers for other library components
// These need to be implemented as needed
#include "drawbuff.h"
#include "buffer.h"
#include "font.h"
#include "iff.h"
#include "misc.h"
#include "mono.h"
#include "tile.h"
#include "wwmem.h"
#include "file.h"
#include "rawfile.h"
#include "audio.h"
#include "dipthong.h"
// Include root facing.h for FacingClass definition
#include "../facing.h"
// Declare facing calculation functions (implemented in ATARILIB/facing.cpp)
#ifdef __cplusplus
extern "C" {
#endif
int Desired_Facing256(long srcx, long srcy, long dstx, long dsty);
int Desired_Facing8(long x1, long y1, long x2, long y2);
#ifdef __cplusplus
}
#endif
#include "palette.h"
#include "playcd.h"
#include "shape.h"
#include "timer.h"
#include "ww_win.h"
#include "wsa.h"
#include "profile.h"

#endif // WWLIB32_H
