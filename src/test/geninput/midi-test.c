#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
// clang-format off
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
// clang-format on
#include <stdio.h>
#include <string.h>

#include "geninput/midi-state.h"

struct test_port {
    void(CALLBACK *callback)(HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
    DWORD_PTR instance;
    bool open;
};

static struct test_port ports[2];
static UINT present = 2;
static bool reversed;
static bool interface_supported = true;
static MMRESULT open_error;
static MMRESULT start_error;
static MMRESULT close_error;
static unsigned int opens;
static unsigned int closes;
static uint64_t test_now;
static HANDLE enumeration_entered;
static HANDLE enumeration_release;

static UINT test_port_index(UINT index)
{
    return reversed ? 1 - index : index;
}

static UINT WINAPI test_num_devs(void)
{
    if (enumeration_entered != NULL) {
        SetEvent(enumeration_entered);
        assert(WaitForSingleObject(enumeration_release, 5000) == WAIT_OBJECT_0);
    }
    return present;
}

static MMRESULT WINAPI test_caps(UINT_PTR index, MIDIINCAPSW *caps, UINT size)
{
    assert(size == sizeof(*caps));
    memset(caps, 0, size);
    caps->wMid = 1;
    caps->wPid = (WORD) (test_port_index(index) + 1);
    wcscpy(caps->szPname, L"Test drum input");
    return MMSYSERR_NOERROR;
}

static DWORD WINAPI
test_message(HMIDIIN index, UINT message, DWORD_PTR first, DWORD_PTR second)
{
    wchar_t name[64];

    if (!interface_supported) {
        return MMSYSERR_NOTSUPPORTED;
    }
    swprintf(name, 64, L"test-interface-%u", test_port_index((UINT_PTR) index));
    if (message == DRV_QUERYDEVICEINTERFACESIZE) {
        *(ULONG *) first = (ULONG) ((wcslen(name) + 1) * sizeof(wchar_t));
    } else {
        assert(message == DRV_QUERYDEVICEINTERFACE);
        assert(second >= (wcslen(name) + 1) * sizeof(wchar_t));
        wcscpy((wchar_t *) first, name);
    }
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_open(
    HMIDIIN *handle,
    UINT index,
    DWORD_PTR callback,
    DWORD_PTR instance,
    DWORD flags)
{
    struct test_port *port = &ports[test_port_index(index)];

    assert((flags & CALLBACK_FUNCTION) != 0);
    if (open_error != 0) {
        return open_error;
    }
    assert(!port->open);
    port->callback = (void(CALLBACK *)(
        HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)) callback;
    port->instance = instance;
    port->open = true;
    *handle = (HMIDIIN) port;
    opens++;
    port->callback(*handle, MIM_OPEN, instance, 0, 0);
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_start(HMIDIIN handle)
{
    return start_error;
}

static DWORD WINAPI test_inflight_callback(void *context)
{
    struct test_port *port = context;

    port->callback((HMIDIIN) port, MIM_DATA, port->instance, 0x00643C99, 0);
    return 0;
}

static MMRESULT WINAPI test_reset(HMIDIIN handle)
{
    HANDLE callback =
        CreateThread(NULL, 0, test_inflight_callback, handle, 0, NULL);

    /* A reset under the callback's lock would deadlock here. */
    assert(callback != NULL);
    assert(WaitForSingleObject(callback, 2000) == WAIT_OBJECT_0);
    CloseHandle(callback);
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_stop(HMIDIIN handle)
{
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_close(HMIDIIN handle)
{
    struct test_port *port = (struct test_port *) handle;

    assert(port->open);
    if (close_error != MMSYSERR_NOERROR) {
        return close_error;
    }
    port->open = false;
    closes++;
    port->callback(handle, MIM_CLOSE, port->instance, 0, 0);
    return MMSYSERR_NOERROR;
}

static ULONGLONG WINAPI test_tick(void)
{
    return test_now;
}

#define midiInGetNumDevs test_num_devs
#define midiInGetDevCapsW test_caps
#define midiInMessage test_message
#define midiInOpen test_open
#define midiInStart test_start
#define midiInStop test_stop
#define midiInReset test_reset
#define midiInClose test_close
#define GetTickCount64 test_tick
#include "geninput/midi.c"
#undef GetTickCount64
#include "config/snap.c"
#include "config/usages.c"
#include "geninput/dev-list.c"
#include "geninput/hid-mgr.c"
#include "geninput/hid.c"
#include "geninput/kbd-data.c"
#include "geninput/kbd-naive.c"
#include "geninput/mapper-s11n.c"
#include "geninput/mapper.c"
#include "geninput/midi-state.c"

static void send_message(unsigned int port, UINT type, uint32_t data)
{
    assert(ports[port].open);
    ports[port].callback(
        (HMIDIIN) &ports[port], type, ports[port].instance, data, 0);
}

static void test_protocol(void)
{
    struct midi_state state;
    unsigned int i;

    midi_state_reset(&state);
    assert(midi_state_value(&state, MIDI_PITCH + 9) == 8192);
    midi_state_message(&state, 0x00643C99);
    midi_state_message(&state, 0x00003C89);
    midi_state_poll(&state, 100, true, false);
    assert(midi_state_value(&state, 9 * 128 + 60) == 1);
    assert(midi_state_value(&state, 60) == 0);
    midi_state_poll(&state, 119, true, false);
    assert(midi_state_value(&state, 9 * 128 + 60) == 1);
    midi_state_poll(&state, 120, true, false);
    assert(midi_state_value(&state, 9 * 128 + 60) == 0);
    midi_state_message(&state, 0x00003C99);
    assert(state.notes[9 * 128 + 60].pending == 0);

    for (i = 0; i < 32; i++) {
        midi_state_message(&state, 0x007F3C99);
    }
    for (i = 0; i < 32; i++) {
        uint64_t now = 200 + i * 22;
        midi_state_poll(&state, now, true, false);
        assert(midi_state_value(&state, 9 * 128 + 60) == 1);
        midi_state_poll(&state, now + 20, true, false);
        assert(midi_state_value(&state, 9 * 128 + 60) == 0);
    }
    assert(state.notes[9 * 128 + 60].pending == 0);

    midi_state_message(&state, 0x007F4099);
    midi_state_poll(&state, 1000, false, false);
    midi_state_poll(&state, 5000, false, false);
    assert(midi_state_value(&state, 9 * 128 + 64) == 1);
    midi_state_message(&state, 0x00004099);
    midi_state_poll(&state, 5001, false, false);
    assert(midi_state_value(&state, 9 * 128 + 64) == 0);

    midi_state_message(&state, 0x003F04B9);
    assert(midi_state_value(&state, MIDI_CC_BUTTON + 9 * 128 + 4) == 0);
    midi_state_message(&state, 0x004004B9);
    assert(midi_state_value(&state, MIDI_CC_BUTTON + 9 * 128 + 4) == 1);
    assert(midi_state_value(&state, MIDI_CC_VALUE + 9 * 128 + 4) == 64);
    midi_state_message(&state, 0x007F7FE9);
    assert(midi_state_value(&state, MIDI_PITCH + 9) == 16383);
    midi_state_message(&state, 0x000079B9);
    assert(midi_state_value(&state, MIDI_PITCH + 9) == 8192);
    assert(midi_state_value(&state, MIDI_CC_VALUE + 9 * 128 + 4) == 0);

    midi_state_message(&state, 0x007F4099);
    midi_state_message(&state, 0x00007BB9);
    assert(state.notes[9 * 128 + 64].pending == 0);
    midi_state_message(&state, 0x007F4099);
    midi_state_message(&state, 0x000000FF);
    assert(state.notes[9 * 128 + 64].pending == 0);
    for (i = 0; i < MIDI_PENDING_LIMIT + 50; i++) {
        midi_state_message(&state, 0x007F4099);
    }
    assert(state.overflow);
    assert(state.notes[9 * 128 + 64].pending == MIDI_PENDING_LIMIT);
    midi_state_message(&state, 0x007F8099);
    midi_state_message(&state, 0x00804099);
    puts(
        "PASS: short notes, channels, 32 queued hits, holds, CC, pitch, reset, "
        "bounds");
}

static void test_mapping_and_capture(void)
{
    struct mapper *mapper;
    struct mapped_action action;
    struct hid_stub *stub;
    struct snap before;
    struct snap after;
    FILE *file;
    unsigned int i;

    hid_mgr_init();
    midi_init();
    midi_drum = true;
    midi_scan();
    assert(opens == 2);
    midi_scan();
    assert(opens == 2);
    stub = hid_mgr_get_named_stub("midi:interface:test-interface-0");
    mapper = mapper_impl_create();
    memset(&action, 0, sizeof(action));
    action.hid = stub;
    action.control_no = 9 * 128 + 60;
    action.value_min = action.value_max = 1;
    mapper_impl_set_action_map(mapper, 0, 0, 3, &action);
    mapper_impl_set_action_map(mapper, 1, 0, 4, &action);
    test_now = 100;
    send_message(0, MIM_MOREDATA, 0x007F3C99);
    send_message(0, MIM_DATA, 0x00003C99);
    assert(mapper_impl_update(mapper) == 24);
    assert(mapper_impl_update(mapper) == 24);
    test_now = 120;
    assert(mapper_impl_update(mapper) == 0);

    midi_set_capture(true);
    snap_init(&before);
    send_message(0, MIM_DATA, 0x007F2699);
    send_message(0, MIM_DATA, 0x00002689);
    test_now = 137;
    snap_init(&after);
    assert(snap_find_edge(&after, &before, &action, false));
    assert(action.hid == stub && action.control_no == 9 * 128 + 38);
    assert(!snap_find_edge(&after, &before, &action, true));
    snap_fini(&after);
    snap_fini(&before);
    midi_set_capture(false);

    file = tmpfile();
    assert(file != NULL);
    mapper_impl_config_save(mapper, file);
    mapper_impl_destroy(mapper);
    /* Remove ports while reset starts a concurrent in-flight callback. */
    present = 0;
    midi_scan();
    assert(closes == 2);
    assert(hid_stub_is_attached(stub));
    midi_fini();
    hid_mgr_fini();

    hid_mgr_init();
    midi_init();
    present = 2;
    reversed = true;
    midi_scan();
    rewind(file);
    mapper = mapper_impl_config_load(file);
    fclose(file);
    assert(mapper != NULL);
    test_now = 200;
    send_message(0, MIM_DATA, 0x007F3C99);
    assert(mapper_impl_update(mapper) == 24);
    for (i = 0; i < 3; i++) {
        present = 0;
        midi_scan();
        assert(mapper_impl_update(mapper) == 0);
        present = 2;
        midi_scan();
        send_message(0, MIM_DATA, 0x007F3C99);
        test_now += 100;
        assert(mapper_impl_update(mapper) == 24);
    }
    mapper_impl_destroy(mapper);
    midi_fini();
    hid_mgr_fini();
    assert(opens == closes);
    puts(
        "PASS: real mapper/capture, duplicate bindings, persistence, reorder, "
        "reconnect, callback teardown");
}

static void test_failures_and_async(void)
{
    ULONGLONG started;

    hid_mgr_init();
    midi_init();
    open_error = MMSYSERR_ALLOCATED;
    midi_scan();
    assert(!ports[0].open && !ports[1].open);
    open_error = 0;
    start_error = MMSYSERR_ERROR;
    midi_scan();
    assert(!ports[0].open && !ports[1].open);
    start_error = 0;

    enumeration_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
    enumeration_release = CreateEvent(NULL, TRUE, FALSE, NULL);
    started = GetTickCount64();
    midi_enable("dm");
    assert(GetTickCount64() - started < 1000);
    assert(WaitForSingleObject(enumeration_entered, 2000) == WAIT_OBJECT_0);
    /* Enumeration is deliberately blocked; caller and HID access still run. */
    assert(WaitForSingleObject(midi_thread, 0) == WAIT_TIMEOUT);
    hid_mgr_lock();
    midi_poll();
    hid_mgr_unlock();
    SetEvent(enumeration_release);
    midi_fini();
    CloseHandle(enumeration_entered);
    CloseHandle(enumeration_release);
    enumeration_entered = enumeration_release = NULL;
    hid_mgr_fini();
    assert(opens == closes);
    puts(
        "PASS: busy port, start failure, asynchronous scan, nonblocking HID "
        "access, clean join");
}

static void test_legacy_identity_and_labels(void)
{
    struct hid_stub *stub;
    char text[128];

    hid_mgr_init();
    midi_init();
    present = 0;
    midi_scan();
    present = 2;
    reversed = false;
    interface_supported = false;
    midi_scan();
    stub = hid_mgr_get_named_stub("midi:legacy:0001:0001:0:Test drum input");
    assert(hid_stub_is_attached(stub));
    reversed = true;
    midi_scan();
    assert(
        stub ==
        hid_mgr_get_named_stub("midi:legacy:0001:0001:0:Test drum input"));
    assert(hid_stub_is_attached(stub));
    usages_get(text, sizeof(text), MIDI_USAGE_NOTE | (9 * 128 + 38));
    assert(strcmp(text, "MIDI Ch 10 Note 38") == 0);
    usages_get(text, sizeof(text), MIDI_USAGE_CC_BUTTON | (9 * 128 + 4));
    assert(strcmp(text, "MIDI Ch 10 CC 4") == 0);
    usages_get(text, sizeof(text), MIDI_USAGE_CC_VALUE | 64);
    assert(strcmp(text, "MIDI Ch 1 CC 64 (value)") == 0);
    usages_get(text, sizeof(text), MIDI_USAGE_PITCH | 15);
    assert(strcmp(text, "MIDI Ch 16 Pitch Bend") == 0);
    midi_fini();
    hid_mgr_fini();
    interface_supported = true;
    assert(opens == closes);
    puts(
        "PASS: zero ports, legacy identity fallback, reorder and readable "
        "note/CC/pitch labels");
}

static void test_failed_close_lifetime(void)
{
    struct midi_device *retained[2];
    unsigned int i;

    hid_mgr_init();
    midi_init();
    midi_scan();
    for (i = 0; i < 2; i++) {
        retained[i] = (struct midi_device *) ports[i].instance;
    }
    close_error = MMSYSERR_ERROR;
    midi_fini();
    hid_mgr_fini();
    for (i = 0; i < 2; i++) {
        send_message(i, MIM_DATA, 0x007F3C99);
        assert(retained[i]->state.notes[9 * 128 + 60].pending == 0);
        assert(!retained[i]->enabled);
    }
    close_error = MMSYSERR_NOERROR;
    for (i = 0; i < 2; i++) {
        midi_close_device(retained[i]);
        midi_device_destroy(&retained[i]->hid);
    }
    assert(opens == closes);
    puts(
        "PASS: failed driver close retains disabled callback state beyond HID "
        "teardown");
}

int main(void)
{
    test_protocol();
    test_mapping_and_capture();
    test_failures_and_async();
    test_legacy_identity_and_labels();
    test_failed_close_lifetime();
    puts("All MIDI input tests passed.");
    return 0;
}
