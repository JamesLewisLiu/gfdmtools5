#include <stdlib.h>

#include "geninput/kbd-data.h"
#include "geninput/kbd-naive.h"
#include "geninput/kbd.h"

#include "util/defs.h"
#include "util/mem.h"

struct kbd_naive {
    struct hid super;
    struct hid_control controls[KBD_NAIVE_NCONTROLS];
};

static void kbd_naive_close(struct hid *hid)
{
    free(hid);
}

static bool kbd_naive_get_device_usage(const struct hid *hid, uint32_t *usage)
{
    *usage = KBD_DEVICE_USAGE_KEYBOARD;

    return true;
}

static bool
kbd_naive_get_name(const struct hid *hid, wchar_t *chars, size_t *nchars)
{
    return hid_ri_get_name(chars, nchars, L"Naive keyboard (all devices)");
}

static bool kbd_naive_get_controls(
    const struct hid *hid, struct hid_control *controls, size_t *ncontrols)
{
    const struct kbd_naive *self = containerof(hid, struct kbd_naive, super);

    return hid_ri_get_controls(
        controls, ncontrols, self->controls, lengthof(self->controls));
}

static bool kbd_naive_get_lights(
    const struct hid *hid, struct hid_light *lights, size_t *nlights)
{
    *nlights = 0;

    return true;
}

static bool
kbd_naive_get_value(struct hid *hid, size_t control_no, int32_t *out_value)
{
    *out_value = 0;

    if (control_no >= KBD_NAIVE_NCONTROLS) {
        return false;
    }

    /* Windows exposes both Enter keys as VK_RETURN. Give EAMIO its keypad
       usage as well, without changing the persisted virtual-key indices. */
    if (control_no == KBD_NAIVE_KEYPAD_ENTER) {
        control_no = VK_RETURN;
    }

    /* Poll in both foreground and background. Only the high bit describes
       the current state; the low bit is shared between consumers and must
       never be used for mapper polling. */
    *out_value = (GetAsyncKeyState((int) control_no) & 0x8000) != 0;

    return true;
}

static bool
kbd_naive_set_light(struct hid *hid, size_t light_no, uint32_t intensity)
{
    return false;
}

static const struct hid_vtbl kbd_naive_vtbl = {
    kbd_naive_close,
    kbd_naive_get_device_usage,
    kbd_naive_get_name,
    kbd_naive_get_controls,
    kbd_naive_get_lights,
    kbd_naive_get_value,
    kbd_naive_set_light};

static uint32_t kbd_naive_usage(unsigned int vkey)
{
    unsigned int scan;
    unsigned int code;

    /* NumLock changes the logical VK returned by Windows; these usages must
       still identify the numeric keypad for the existing EAMIO backend. */
    if (vkey >= VK_NUMPAD1 && vkey <= VK_NUMPAD9) {
        return 0x00070059 + vkey - VK_NUMPAD1;
    }

    switch (vkey) {
        case VK_NUMPAD0:
            return 0x00070062;
        case VK_MULTIPLY:
            return 0x00070055;
        case VK_ADD:
            return 0x00070057;
        case VK_SUBTRACT:
            return 0x00070056;
        case VK_DECIMAL:
            return 0x00070063;
        case VK_DIVIDE:
            return 0x00070054;
        case VK_RETURN:
            return 0x00070028;
        case VK_PAUSE:
            return 0x00070048;
        case VK_NUMLOCK:
            return 0x00070053;
    }

    scan = MapVirtualKeyW(vkey, MAPVK_VK_TO_VSC_EX);
    code = 0;

    if ((scan & 0xFF00) == 0xE000) {
        scan &= 0xFF;

        if (scan < kbd_ext_nusages) {
            code = kbd_ext_usages[scan];
        }
    } else if (scan < kbd_basic_nusages) {
        code = kbd_basic_usages[scan];
    }

    /* Preserve otherwise unmapped virtual keys in a vendor usage page. */
    return code != 0 ? 0x00070000 | code : 0xFF000000 | vkey;
}

void kbd_naive_create(struct hid **hid)
{
    struct kbd_naive *self;
    size_t i;

    self = xcalloc(sizeof(*self));
    self->super.vptr = &kbd_naive_vtbl;

    for (i = 0; i < lengthof(self->controls); i++) {
        self->controls[i].usage =
            i == KBD_NAIVE_KEYPAD_ENTER ? 0x00070058 : kbd_naive_usage(i);
        self->controls[i].value_max = 1;
    }

    *hid = &self->super;
}
