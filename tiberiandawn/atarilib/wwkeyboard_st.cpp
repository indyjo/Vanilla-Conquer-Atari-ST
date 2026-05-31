/*
 * wwkeyboard_st.cpp - Atari ST keyboard driver for Vanilla Conquer WWKeyboardClass
 */

#include "common/wwkeyboard.h"
#include "ikbd.h"
#include <ctype.h>

class WWKeyboardClassST : public WWKeyboardClass
{
public:
    WWKeyboardClassST() = default;
    ~WWKeyboardClassST() override = default;

    KeyASCIIType To_ASCII(unsigned short key) override;
    void Fill_Buffer_From_System() override;
};

void WWKeyboardClassST::Fill_Buffer_From_System()
{
    IKBD_Service();
    IKBD_Get_Mouse_XY(&MouseQX, &MouseQY);

    unsigned char event_byte = 0;
    while (!Is_Buffer_Full() && IKBD_Pop_Event(&event_byte)) {
        int vk = (int)(event_byte & IKBD_EVENT_KEY_MASK);
        bool release = (event_byte & IKBD_EVENT_RELEASE_BIT) != 0;

        if (!Put_Key_Message((unsigned short)vk, release)) {
            break;
        }

        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
            if (!Put((unsigned short)MouseQX) || !Put((unsigned short)MouseQY)) {
                break;
            }
        }
    }
}

KeyASCIIType WWKeyboardClassST::To_ASCII(unsigned short key)
{
    if (key & WWKEY_RLS_BIT) {
        return KA_NONE;
    }

    key &= 0xFF;

    if (key >= 32 && key < 127) {
        return (KeyASCIIType)key;
    }

    switch (key) {
    case VK_RETURN:
        return KA_RETURN;
    case VK_ESCAPE:
        return KA_ESC;
    case VK_TAB:
        return KA_TAB;
    case VK_BACK:
        return KA_BACKSPACE;
    case VK_SPACE:
        return KA_SPACE;
    default:
        break;
    }

    return KA_NONE;
}

static WWKeyboardClassST STKeyboard;

WWKeyboardClass* CreateWWKeyboardClass(void)
{
    return &STKeyboard;
}
