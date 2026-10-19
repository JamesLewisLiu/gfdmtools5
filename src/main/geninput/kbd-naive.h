/**
 * Device-independent, foreground-only Windows virtual-key keyboard.
 * Control numbers 0..255 are stable virtual-key codes in saved mappings.
 */
#ifndef GENINPUT_KBD_NAIVE_H
#define GENINPUT_KBD_NAIVE_H

#include "geninput/hid.h"

#define KBD_NAIVE_DEV_NODE "bemanitools:keyboard:naive"
#define KBD_NAIVE_KEYPAD_ENTER 256
#define KBD_NAIVE_NCONTROLS 257

/** Create a keyboard owned and closed by the HID manager. */
void kbd_naive_create(struct hid **hid);

#endif
