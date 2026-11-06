/**
 * Device-independent Windows virtual-key keyboard, active in the background.
 * Control numbers 0..255 are stable virtual-key codes in saved mappings.
 */
#ifndef GENINPUT_KBD_NAIVE_H
#define GENINPUT_KBD_NAIVE_H

#include "geninput/hid.h"

#define KBD_NAIVE_DEV_NODE "bemanitools:keyboard:naive"
#define KBD_NAIVE_KEYPAD_ENTER 256
#define KBD_NAIVE_NCONTROLS 257

/* Some keyboard layouts return a non-extended keypad scan for navigation VKs,
   even for MAPVK_VK_TO_VSC_EX. Preserve their dedicated E0 identity in both
   HID usage metadata and GetKeyNameText, without changing saved VK indices. */
static inline UINT kbd_naive_scan_code(UINT vkey)
{
    switch (vkey) {
        case VK_PRIOR: return 0xE049;
        case VK_NEXT: return 0xE051;
        case VK_END: return 0xE04F;
        case VK_HOME: return 0xE047;
        case VK_LEFT: return 0xE04B;
        case VK_UP: return 0xE048;
        case VK_RIGHT: return 0xE04D;
        case VK_DOWN: return 0xE050;
        case VK_INSERT: return 0xE052;
        case VK_DELETE: return 0xE053;
        default: return MapVirtualKeyW(vkey, MAPVK_VK_TO_VSC_EX);
    }
}

/** Create a keyboard owned and closed by the HID manager. */
void kbd_naive_create(struct hid **hid);

#endif
