#ifndef ATARI_IKBD_H
#define ATARI_IKBD_H

#include "../COMMONLIB/wwstd.h"
#include "windows.h"

/*
 * Atari ST keyboard/mouse input.
 *
 * Two independent ingress paths share the same relative-mouse apply routine
 * and the same key/button ring. They must not share the ACIA byte-parser
 * state machine.
 *
 * 1. MFP vector $118 (IKBD ACIA at $FFFFFC00/$FC02)
 *    Hardware 6301 (keyboard + DB9 mouse). We own this vector while the
 *    game runs. TOS ikbdsys does not see those bytes, so TOS never calls
 *    mousevec for the ST mouse.
 *
 * 2. KBDVECS.mousevec (BIOS Kbdvbase(), XBRA 'CNCM')
 *    Software packets only. USB4TOS / FreeMiNT mouse.udd converts HID
 *    reports to 3-byte IKBD packets and calls the current mousevec
 *    (A0 -> $F8..$FB, dx, dy). It never writes the ACIA. Wheel / extra
 *    buttons go through kbdvec/IOREC and are not handled here.
 *
 * Install (Supexec): IKBD reset + relative mode, hook $118, then hook
 * mousevec. Shutdown: unhook mousevec first (so USB hits TOS again),
 * restore IKBD desktop commands, drain ACIA, restore $118. Always call
 * IKBD_Uninstall() on every exit path, including OOM.
 *
 * mousevec runs in supervisor mode (TOS/USB contract). The handler raises
 * IPL 7 around the apply so a $118 byte cannot tear MouseX/buttons. The
 * previous mousevec is chained after apply (LINE-A / other XBRA clients).
 *
 * Compact ring entry: bit7 = release, bit6..0 = logical key id (VK-like).
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
