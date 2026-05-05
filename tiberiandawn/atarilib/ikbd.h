#ifndef ATARI_IKBD_H
#define ATARI_IKBD_H

#include "../COMMONLIB/wwstd.h"
#include "windows.h"

/*
** Compact ring entry format:
** bit7 = release flag, bit6..0 = logical key id (VK-compatible where possible).
*/
#define IKBD_EVENT_RELEASE_BIT 0x80u
#define IKBD_EVENT_KEY_MASK    0x7Fu

BOOL IKBD_Install(void);
void IKBD_Uninstall(void);

/* Poll pending IKBD bytes and feed the parser (safe outside interrupt context). */
void IKBD_Service(void);

/* Pop one compact event from ring. Returns FALSE if empty. */
BOOL IKBD_Pop_Event(unsigned char *event_byte);

/* Mouse position as tracked from IKBD relative packets. */
void IKBD_Get_Mouse_XY(int *x, int *y);

/* Key/button hold state from make/break stream. */
int IKBD_Key_Is_Down(int vk);

/* Overflow status for compact event ring; read-only signal for diagnostics. */
int IKBD_Event_Overflowed(void);

#endif
