/*
 * keyboard.cpp - Legacy keyboard helper wrappers for Atari ST/MiNT
 */

#include "function.h"
#include "ikbd.h"

int Get_Key_Num(void)
{
    if (!Keyboard) {
        return KN_NONE;
    }

    KeyNumType key = Keyboard->Get();
    return (int)(key & ~WWKEY_SHIFT_BIT);
}

int Check_Key(void)
{
    if (!Keyboard) {
        return KN_NONE;
    }

    KeyNumType key = Keyboard->Check();
    return (int)(key & ~WWKEY_SHIFT_BIT);
}

int Check_Key_Num(void)
{
    if (!Keyboard) {
        return KN_NONE;
    }

    KeyNumType key = Keyboard->Check();
    if (key == KN_NONE) {
        return KN_NONE;
    }
    return (int)(key & ~WWKEY_SHIFT_BIT);
}

void Clear_KeyBuffer(void)
{
    if (Keyboard) {
        Keyboard->Clear();
    }
}

void Stuff_Key_Num(int key)
{
    if (Keyboard) {
        Keyboard->Put((unsigned short)key);
    }
}

int Get_Key(void)
{
    if (!Keyboard) {
        return KN_NONE;
    }

    KeyNumType retval = Keyboard->Get();
    if (retval & WWKEY_RLS_BIT) {
        return KN_NONE;
    }
    return (int)(retval & ~WWKEY_SHIFT_BIT);
}

int KN_To_VK(int key)
{
    if (!Keyboard) {
        return key;
    }

    int flags = key & (WWKEY_SHIFT_BIT | WWKEY_CTRL_BIT | WWKEY_ALT_BIT);
    key &= 0x00FF;
    return key | flags;
}
