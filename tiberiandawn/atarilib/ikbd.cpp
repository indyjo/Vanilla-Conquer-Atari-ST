#include "ikbd.h"
#include "keyboard.h"

#include <mint/osbind.h>
#include <stdio.h>

enum IKBDParseStateType {
	IKBD_PARSE_NORMAL = 0,
	IKBD_PARSE_REL_DX,
	IKBD_PARSE_REL_DY,
	IKBD_PARSE_SKIP
};

static volatile unsigned char EventRing[256];
static volatile unsigned char EventHead = 0;
static volatile unsigned char EventTail = 0;
static volatile unsigned char EventOverflow = 0;

static volatile unsigned char KeyDown[128];

static volatile int MouseX = 0;
static volatile int MouseY = 0;
static volatile unsigned char MouseButtons = 0;
static volatile unsigned char LastRawHeaderBits = 0;
static volatile unsigned long MousePacketCount = 0;

static volatile unsigned char ParseState = IKBD_PARSE_NORMAL;
static volatile unsigned char SkipCount = 0;
static volatile unsigned char RelHeader = 0;
static volatile signed char RelDX = 0;

enum {
	IKBD_MOUSE_WIDTH = 320,
	IKBD_MOUSE_HEIGHT = 200
};

static void (*PrevIKBDVector)(void) = NULL;
static volatile unsigned char HandlerInstalled = 0;

static volatile unsigned char * const IKBD_ACIA_STATUS = (volatile unsigned char *)0xFFFFFC00UL;
static volatile unsigned char * const IKBD_ACIA_DATA = (volatile unsigned char *)0xFFFFFC02UL;
static volatile unsigned char * const MFP_ISRA = (volatile unsigned char *)0xFFFFFA11UL;
static const short IKBD_VECTOR_NUMBER = 0x46; /* vector at address $118 */

static inline int IKBD_Clamp_Mouse_X(int x)
{
	if (x < 0) {
		return 0;
	}
	if (x >= IKBD_MOUSE_WIDTH) {
		return IKBD_MOUSE_WIDTH - 1;
	}
	return x;
}

static inline int IKBD_Clamp_Mouse_Y(int y)
{
	if (y < 0) {
		return 0;
	}
	if (y >= IKBD_MOUSE_HEIGHT) {
		return IKBD_MOUSE_HEIGHT - 1;
	}
	return y;
}

static inline void IKBD_Push_Event(unsigned char event_byte)
{
	unsigned char next = (unsigned char)(EventTail + 1u);
	if (next == EventHead) {
		EventOverflow = 1;
		return;
	}
	EventRing[EventTail] = event_byte;
	EventTail = next;
}

static inline int IKBD_Map_Scan_To_VK(unsigned char scan)
{
	switch (scan) {
		case 0x01: return VK_ESCAPE;
		case 0x02: return '1';
		case 0x03: return '2';
		case 0x04: return '3';
		case 0x05: return '4';
		case 0x06: return '5';
		case 0x07: return '6';
		case 0x08: return '7';
		case 0x09: return '8';
		case 0x0A: return '9';
		case 0x0B: return '0';
		case 0x0C: return '-';
		case 0x0D: return '=';
		case 0x0E: return VK_BACK;
		case 0x0F: return VK_TAB;
		case 0x10: return 'Q';
		case 0x11: return 'W';
		case 0x12: return 'E';
		case 0x13: return 'R';
		case 0x14: return 'T';
		case 0x15: return 'Y';
		case 0x16: return 'U';
		case 0x17: return 'I';
		case 0x18: return 'O';
		case 0x19: return 'P';
		case 0x1A: return '[';
		case 0x1B: return ']';
		case 0x1C: return VK_RETURN;
		case 0x1D: return VK_CONTROL;
		case 0x1E: return 'A';
		case 0x1F: return 'S';
		case 0x20: return 'D';
		case 0x21: return 'F';
		case 0x22: return 'G';
		case 0x23: return 'H';
		case 0x24: return 'J';
		case 0x25: return 'K';
		case 0x26: return 'L';
		case 0x27: return ';';
		case 0x28: return '\'';
		case 0x29: return '`';
		case 0x2A: return VK_SHIFT;
		case 0x2B: return '\\';
		case 0x2C: return 'Z';
		case 0x2D: return 'X';
		case 0x2E: return 'C';
		case 0x2F: return 'V';
		case 0x30: return 'B';
		case 0x31: return 'N';
		case 0x32: return 'M';
		case 0x33: return ',';
		case 0x34: return '.';
		case 0x35: return '/';
		case 0x36: return VK_SHIFT;
		case 0x38: return VK_MENU;
		case 0x39: return VK_SPACE;
		case 0x3A: return VK_CAPITAL;
		case 0x3B: return VK_F1;
		case 0x3C: return VK_F2;
		case 0x3D: return VK_F3;
		case 0x3E: return VK_F4;
		case 0x3F: return VK_F5;
		case 0x40: return VK_F6;
		case 0x41: return VK_F7;
		case 0x42: return VK_F8;
		case 0x43: return VK_F9;
		case 0x44: return VK_F10;
		case 0x47: return VK_HOME;
		case 0x48: return VK_UP;
		case 0x4A: return '-';
		case 0x4B: return VK_LEFT;
		case 0x4D: return VK_RIGHT;
		case 0x4E: return '+';
		case 0x50: return VK_DOWN;
		case 0x52: return VK_INSERT;
		case 0x53: return VK_DELETE;
		case 0x61: return VK_F11;
		case 0x62: return VK_F12;
		default:   return 0;
	}
}

static inline void IKBD_Handle_Key_Scan(unsigned char scan_with_break)
{
	int release = (scan_with_break & 0x80u) != 0;
	unsigned char scan = (unsigned char)(scan_with_break & 0x7Fu);
	int vk = IKBD_Map_Scan_To_VK(scan);
	if (vk <= 0 || vk > 127) {
		return;
	}

	KeyDown[vk] = (unsigned char)(release ? 0 : 1);
	IKBD_Push_Event((unsigned char)((vk & IKBD_EVENT_KEY_MASK) | (release ? IKBD_EVENT_RELEASE_BIT : 0)));
}

static inline void IKBD_Handle_Mouse_Buttons(unsigned char new_buttons)
{
	unsigned char changed = (unsigned char)(MouseButtons ^ new_buttons);
	if ((changed & 0x01u) != 0u) {
		if ((new_buttons & 0x01u) != 0u) {
			KeyDown[VK_LBUTTON] = 1;
			IKBD_Push_Event((unsigned char)(VK_LBUTTON & IKBD_EVENT_KEY_MASK));
		} else {
			KeyDown[VK_LBUTTON] = 0;
			IKBD_Push_Event((unsigned char)((VK_LBUTTON & IKBD_EVENT_KEY_MASK) | IKBD_EVENT_RELEASE_BIT));
		}
	}
	if ((changed & 0x02u) != 0u) {
		if ((new_buttons & 0x02u) != 0u) {
			KeyDown[VK_RBUTTON] = 1;
			IKBD_Push_Event((unsigned char)(VK_RBUTTON & IKBD_EVENT_KEY_MASK));
		} else {
			KeyDown[VK_RBUTTON] = 0;
			IKBD_Push_Event((unsigned char)((VK_RBUTTON & IKBD_EVENT_KEY_MASK) | IKBD_EVENT_RELEASE_BIT));
		}
	}
	MouseButtons = new_buttons;
}

static inline void IKBD_Parse_Byte(unsigned char value)
{
	if (ParseState == IKBD_PARSE_SKIP) {
		if (SkipCount > 0) {
			SkipCount--;
		}
		if (SkipCount == 0) {
			ParseState = IKBD_PARSE_NORMAL;
		}
		return;
	}

	if (ParseState == IKBD_PARSE_REL_DX) {
		RelDX = (signed char)value;
		ParseState = IKBD_PARSE_REL_DY;
		return;
	}

	if (ParseState == IKBD_PARSE_REL_DY) {
		signed char dy = (signed char)value;
		MouseX = IKBD_Clamp_Mouse_X(MouseX + (int)RelDX);
		MouseY = IKBD_Clamp_Mouse_Y(MouseY + (int)dy);
		/*
		** Header layout is %111110xy where x=left, y=right.
		** Normalize to bit0=left, bit1=right with 1=pressed.
		** On this target/TOS setup, x/y arrive active-high.
		*/
		{
			unsigned char raw = (unsigned char)(RelHeader & 0x03u);
			LastRawHeaderBits = raw;
			MousePacketCount++;
			unsigned char buttons = 0;
			if ((raw & 0x02u) != 0u) {
				buttons |= 0x01u; /* left */
			}
			if ((raw & 0x01u) != 0u) {
				buttons |= 0x02u; /* right */
			}
			IKBD_Handle_Mouse_Buttons(buttons);
		}
		ParseState = IKBD_PARSE_NORMAL;
		return;
	}

	if (value >= 0xF8u && value <= 0xFBu) {
		RelHeader = value;
		ParseState = IKBD_PARSE_REL_DX;
		return;
	}

	switch (value) {
		case 0xF6u:
			ParseState = IKBD_PARSE_SKIP;
			SkipCount = 7;
			return;
		case 0xF7u:
			ParseState = IKBD_PARSE_SKIP;
			SkipCount = 5;
			return;
		case 0xFCu:
			ParseState = IKBD_PARSE_SKIP;
			SkipCount = 6;
			return;
		case 0xFDu:
			ParseState = IKBD_PARSE_SKIP;
			SkipCount = 2;
			return;
		case 0xFEu:
		case 0xFFu:
			ParseState = IKBD_PARSE_SKIP;
			SkipCount = 1;
			return;
		default:
			break;
	}

	if (value < 0xF6u) {
		IKBD_Handle_Key_Scan(value);
	}
}

static inline void IKBD_Read_Acia_Bytes(void)
{
	while (((*IKBD_ACIA_STATUS) & 0x01u) != 0u) {
		IKBD_Parse_Byte((unsigned char)(*IKBD_ACIA_DATA));
	}
}

extern "C" void IKBD_ISR_Entry(void) __attribute__((interrupt_handler));

extern "C" void IKBD_ISR_Entry(void)
{
	IKBD_Read_Acia_Bytes();
	/*
	** End of interrupt for MFP group A, bit 6 (IKBD ACIA).
	*/
	*MFP_ISRA = (unsigned char)(*MFP_ISRA & (unsigned char)~0x40u);
}

static long IKBD_Install_Supervisor(void)
{
	char set_relative_mouse = 0x08;
	Ikbdws(1, &set_relative_mouse);
	MouseX = IKBD_MOUSE_WIDTH / 2;
	MouseY = IKBD_MOUSE_HEIGHT / 2;
	PrevIKBDVector = (void (*)(void))Setexc(IKBD_VECTOR_NUMBER, (void (*)())IKBD_ISR_Entry);
	HandlerInstalled = 1;
	return 1;
}

static long IKBD_Uninstall_Supervisor(void)
{
	if (HandlerInstalled && PrevIKBDVector) {
		Setexc(IKBD_VECTOR_NUMBER, (void (*)())PrevIKBDVector);
	}
	HandlerInstalled = 0;
	PrevIKBDVector = NULL;
	return 1;
}

BOOL IKBD_Install(void)
{
	if (HandlerInstalled) {
		printf("IKBD: interrupt handler already installed.\n");
		return TRUE;
	}
	BOOL ok = Supexec(IKBD_Install_Supervisor) != 0 ? TRUE : FALSE;
	printf("IKBD: interrupt handler %s.\n", ok ? "installed" : "install failed");
	return ok;
}

void IKBD_Uninstall(void)
{
	if (!HandlerInstalled) {
		printf("IKBD: interrupt handler not installed.\n");
		return;
	}
	Supexec(IKBD_Uninstall_Supervisor);
	printf("IKBD: interrupt handler uninstalled.\n");
}

void IKBD_Service(void)
{
	/*
	** Bytes are consumed by the IKBD ACIA interrupt handler.
	*/
}

BOOL IKBD_Pop_Event(unsigned char *event_byte)
{
	if (!event_byte) {
		return FALSE;
	}
	if (EventHead == EventTail) {
		return FALSE;
	}
	*event_byte = EventRing[EventHead];
	EventHead = (unsigned char)(EventHead + 1u);
	return TRUE;
}

void IKBD_Get_Mouse_XY(int *x, int *y)
{
	if (x) {
		*x = MouseX;
	}
	if (y) {
		*y = MouseY;
	}
}

int IKBD_Key_Is_Down(int vk)
{
	vk &= 0x7F;
	if (vk == VK_SHIFT) {
		return (int)(KeyDown[VK_SHIFT] | KeyDown[VK_LSHIFT] | KeyDown[VK_RSHIFT]);
	}
	if (vk == VK_CONTROL) {
		return (int)(KeyDown[VK_CONTROL] | KeyDown[VK_LCONTROL] | KeyDown[VK_RCONTROL]);
	}
	if (vk == VK_MENU) {
		return (int)(KeyDown[VK_MENU] | KeyDown[VK_LMENU] | KeyDown[VK_RMENU]);
	}
	return (vk >= 0 && vk < 128) ? (int)KeyDown[vk] : 0;
}

int IKBD_Event_Overflowed(void)
{
	return (int)EventOverflow;
}
