#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "config/snap.h"
#include "geninput/hid-mgr.h"
#include "geninput/kbd-naive.h"
#include "geninput/kbd.h"
#include "geninput/mapper-s11n.h"

static SHORT test_keys[256];
static HWND test_foreground;
static DWORD test_foreground_pid;

static SHORT WINAPI test_get_async_key_state(int vkey)
{
    assert(vkey >= 0 && vkey < 256);

    return test_keys[vkey];
}

static HWND WINAPI test_get_foreground_window(void)
{
    return test_foreground;
}

static DWORD WINAPI test_get_window_thread_process_id(HWND hwnd, DWORD *pid)
{
    assert(hwnd == test_foreground);
    *pid = test_foreground_pid;

    return 1;
}

/* Exercise the production backend with deterministic Windows input/focus.
   The real HID manager, mapper, serialization and capture code remain intact.
 */
#define GetAsyncKeyState test_get_async_key_state
#define GetForegroundWindow test_get_foreground_window
#define GetWindowThreadProcessId test_get_window_thread_process_id
#include "geninput/kbd-naive.c"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId

static void test_reset(void)
{
    memset(test_keys, 0, sizeof(test_keys));
    test_foreground = (HWND) (uintptr_t) 1;
    test_foreground_pid = GetCurrentProcessId();
}

static int32_t test_read(struct hid_stub *stub, size_t control)
{
    int32_t value;

    assert(hid_stub_get_value(stub, control, &value));

    return value;
}

static void test_keyboard(void)
{
    struct hid_stub *stub;
    struct hid_control controls[KBD_NAIVE_NCONTROLS];
    size_t ncontrols;
    uint32_t usage;
    wchar_t name[64];
    size_t nchars;

    test_reset();
    hid_mgr_init();
    stub = hid_mgr_get_named_stub(KBD_NAIVE_DEV_NODE);
    assert(hid_stub_is_attached(stub));
    assert(hid_stub_get_device_usage(stub, &usage));
    assert(usage == KBD_DEVICE_USAGE_KEYBOARD);
    nchars = lengthof(name);
    assert(hid_stub_get_name(stub, name, &nchars));
    assert(wcsstr(name, L"Naive") != NULL);
    ncontrols = 0;
    assert(hid_stub_get_controls(stub, NULL, &ncontrols));
    assert(ncontrols == KBD_NAIVE_NCONTROLS);
    assert(hid_stub_get_controls(stub, controls, &ncontrols));
    assert(controls[VK_NUMPAD0].usage == 0x00070062);
    assert(controls[VK_NUMPAD9].usage == 0x00070061);
    assert(controls[VK_ADD].usage == 0x00070057);
    assert(controls[VK_DECIMAL].usage == 0x00070063);
    assert(controls[VK_RETURN].usage == 0x00070028);
    assert(controls[KBD_NAIVE_KEYPAD_ENTER].usage == 0x00070058);
    assert(controls[VK_LSHIFT].usage == 0x000700E1);
    assert(controls[VK_RSHIFT].usage == 0x000700E5);
    assert(controls[VK_LCONTROL].usage == 0x000700E0);
    assert(controls[VK_RCONTROL].usage == 0x000700E4);
    assert(controls[VK_LMENU].usage == 0x000700E2);
    assert(controls[VK_RMENU].usage == 0x000700E6);

    test_keys[VK_F2] = 1;
    assert(test_read(stub, VK_F2) == 0);
    test_keys[VK_F2] = (SHORT) 0x8000;
    assert(test_read(stub, VK_F2) == 1);
    assert(test_read(stub, VK_F2) == 1);
    test_foreground_pid++;
    assert(test_read(stub, VK_F2) == 0);
    test_foreground = NULL;
    assert(test_read(stub, VK_F2) == 0);
    test_reset();
    test_keys[VK_RETURN] = (SHORT) 0x8000;
    assert(test_read(stub, VK_RETURN) == 1);
    assert(test_read(stub, KBD_NAIVE_KEYPAD_ENTER) == 1);
    test_keys[VK_RCONTROL] = (SHORT) 0x8000;
    assert(test_read(stub, VK_LCONTROL) == 0);
    assert(test_read(stub, VK_RCONTROL) == 1);
    hid_mgr_fini();
    puts("PASS: keyboard metadata, keypad, high bit, focus, modifiers, Enter");
}

static void test_capture(void)
{
    struct snap before;
    struct snap after;
    struct mapped_action ma;
    struct hid *physical;
    struct hid_stub *stub;

    test_reset();
    hid_mgr_init();
    kbd_naive_create(&physical);
    stub = hid_mgr_get_named_stub("test:physical-keyboard");
    hid_stub_attach(stub, physical);
    snap_init(&before);
    test_keys[VK_LBUTTON] = (SHORT) 0x8000;
    test_keys[VK_SHIFT] = (SHORT) 0x8000;
    test_keys[VK_RSHIFT] = (SHORT) 0x8000;
    snap_init(&after);
    assert(snap_find_edge(&after, &before, &ma, true));
    assert(strcmp(hid_stub_get_dev_node(ma.hid), KBD_NAIVE_DEV_NODE) == 0);
    assert(ma.control_no == VK_RSHIFT);
    assert(ma.value_min == 1 && ma.value_max == 1);
    assert(snap_find_edge(&after, &before, &ma, false));
    assert(ma.hid == stub);
    assert(!snap_find_edge(&after, &after, &ma, true));
    snap_fini(&before);
    snap_fini(&after);

    memset(test_keys, 0, sizeof(test_keys));
    snap_init(&before);
    test_keys[VK_ESCAPE] = (SHORT) 0x8000;
    test_keys[VK_LBUTTON] = (SHORT) 0x8000;
    test_keys[VK_NUMLOCK] = (SHORT) 0x8000;
    snap_init(&after);
    assert(!snap_find_edge(&after, &before, &ma, true));
    snap_fini(&before);
    snap_fini(&after);
    hid_mgr_fini();
    puts(
        "PASS: capture separation, held keys, modifiers, mouse/Escape/NumLock");
}

static void test_persistence(void)
{
    struct mapper *mapper;
    struct mapped_action ma;
    FILE *file;

    test_reset();
    hid_mgr_init();
    mapper = mapper_impl_create();
    ma.hid = hid_mgr_get_named_stub(KBD_NAIVE_DEV_NODE);
    ma.control_no = VK_F2;
    ma.value_min = 1;
    ma.value_max = 1;
    mapper_impl_set_action_map(mapper, 0, 0, 3, &ma);
    file = tmpfile();
    assert(file != NULL);
    mapper_impl_config_save(mapper, file);
    assert(!ferror(file));
    mapper_impl_destroy(mapper);
    hid_mgr_fini();

    /* Recreate devices like a fresh process before loading the saved file. */
    hid_mgr_init();
    rewind(file);
    mapper = mapper_impl_config_load(file);
    assert(mapper != NULL);
    fclose(file);
    assert(mapper_impl_get_action_map(mapper, 0, 0, &ma));
    assert(ma.hid == hid_mgr_get_named_stub(KBD_NAIVE_DEV_NODE));
    assert(ma.control_no == VK_F2);
    assert(mapper_impl_update(mapper) == 0);
    test_keys[VK_F2] = (SHORT) 0x8000;
    assert(mapper_impl_update(mapper) == (1ULL << 3));
    test_foreground_pid++;
    assert(mapper_impl_update(mapper) == 0);
    test_reset();
    assert(mapper_impl_update(mapper) == 0);
    mapper_impl_destroy(mapper);
    hid_mgr_fini();
    puts("PASS: saved mapping reload, mapper press/release and focus");
}

int main(void)
{
    test_keyboard();
    test_capture();
    test_persistence();
    puts("All Naive keyboard tests passed.");

    return 0;
}
