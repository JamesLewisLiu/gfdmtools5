/* MIDI 1.0 input adapter for the existing generic input mapper.
   Implemented against the Windows multimedia API; no spice2x source is
   incorporated. Keep driver calls off the UI thread and outside HID locks. */
// clang-format off
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
// clang-format on

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "geninput/hid-mgr.h"
#include "geninput/midi-state.h"
#include "geninput/midi.h"
#include "util/log.h"
#include "util/mem.h"
#include "util/str.h"

struct midi_device {
    struct hid hid;
    struct midi_device *next;
    CRITICAL_SECTION lock;
    struct midi_state state;
    char *identity;
    wchar_t name[MAXPNAMELEN + 16];
    HMIDIIN handle;
    UINT index;
    bool enabled;
    bool seen;
    MMRESULT last_error;
};

static struct midi_device *midi_devices;
static HANDLE midi_stop;
static HANDLE midi_thread;
static bool midi_drum;
static bool midi_capture;

static void midi_device_destroy(struct hid *hid)
{
    struct midi_device *device = (struct midi_device *) hid;

    /* A failed driver close must never leave a callback pointing at freed
       memory. Normal shutdown has already closed every handle on the worker. */
    if (device->handle != NULL) {
        log_warning("MIDI driver did not close; retaining callback state");
        return;
    }
    DeleteCriticalSection(&device->lock);
    free(device->identity);
    free(device);
}

static bool midi_device_usage(const struct hid *hid, uint32_t *usage)
{
    *usage = MIDI_USAGE_NOTE;
    return true;
}

static bool
midi_device_name(const struct hid *hid, wchar_t *chars, size_t *nchars)
{
    const struct midi_device *device = (const struct midi_device *) hid;

    return hid_ri_get_name(chars, nchars, device->name);
}

static bool midi_device_controls(
    const struct hid *hid, struct hid_control *controls, size_t *ncontrols)
{
    unsigned int i;

    if (controls == NULL) {
        *ncontrols = MIDI_CONTROLS;
        return true;
    }
    if (*ncontrols < MIDI_CONTROLS) {
        return false;
    }
    for (i = 0; i < MIDI_CONTROLS; i++) {
        controls[i].value_min = 0;
        controls[i].flags = 0;
        if (i < MIDI_CC_BUTTON) {
            controls[i].usage = MIDI_USAGE_NOTE | i;
            controls[i].value_max = 1;
        } else if (i < MIDI_CC_VALUE) {
            controls[i].usage = MIDI_USAGE_CC_BUTTON | (i - MIDI_CC_BUTTON);
            controls[i].value_max = 1;
        } else if (i < MIDI_PITCH) {
            controls[i].usage = MIDI_USAGE_CC_VALUE | (i - MIDI_CC_VALUE);
            controls[i].value_max = 127;
        } else {
            controls[i].usage = MIDI_USAGE_PITCH | (i - MIDI_PITCH);
            controls[i].value_max = 16383;
        }
    }
    *ncontrols = MIDI_CONTROLS;
    return true;
}

static bool midi_device_lights(
    const struct hid *hid, struct hid_light *lights, size_t *nlights)
{
    *nlights = 0;
    return true;
}

static bool midi_device_value(struct hid *hid, size_t control, int32_t *value)
{
    struct midi_device *device = (struct midi_device *) hid;

    if (control >= MIDI_CONTROLS) {
        return false;
    }
    EnterCriticalSection(&device->lock);
    *value = device->enabled ? midi_state_value(&device->state, control) : 0;
    LeaveCriticalSection(&device->lock);
    return true;
}

static bool midi_device_light(struct hid *hid, size_t light, uint32_t value)
{
    return false;
}

static const struct hid_vtbl midi_vtbl = {
    midi_device_destroy,
    midi_device_usage,
    midi_device_name,
    midi_device_controls,
    midi_device_lights,
    midi_device_value,
    midi_device_light};

static void CALLBACK midi_receive(
    HMIDIIN handle,
    UINT type,
    DWORD_PTR instance,
    DWORD_PTR message,
    DWORD_PTR timestamp)
{
    struct midi_device *device = (struct midi_device *) instance;

    if (type != MIM_DATA && type != MIM_MOREDATA) {
        return;
    }
    /* Never call a multimedia API or acquire hid_mgr's lock in a callback.
       Reset/close may wait for this callback to finish. */
    EnterCriticalSection(&device->lock);
    if (device->enabled) {
        midi_state_message(&device->state, (uint32_t) message);
    }
    LeaveCriticalSection(&device->lock);
}

static char *midi_identity(UINT index, const MIDIINCAPSW *caps, UINT duplicate)
{
    wchar_t *interface = NULL;
    ULONG bytes = 0;
    char *identity;
    char *utf8;
    int size;
    bool has_interface = false;

    if (midiInMessage(
            (HMIDIIN) (UINT_PTR) index,
            DRV_QUERYDEVICEINTERFACESIZE,
            (DWORD_PTR) &bytes,
            0) == MMSYSERR_NOERROR &&
        bytes >= sizeof(wchar_t) && bytes <= 65536 &&
        bytes % sizeof(wchar_t) == 0) {
        interface = xcalloc(bytes + sizeof(wchar_t));
        if (midiInMessage(
                (HMIDIIN) (UINT_PTR) index,
                DRV_QUERYDEVICEINTERFACE,
                (DWORD_PTR) interface,
                bytes) == MMSYSERR_NOERROR &&
            interface[0] != 0) {
            has_interface = true;
        }
    }
    size = WideCharToMultiByte(
        CP_UTF8,
        0,
        has_interface ? interface : caps->szPname,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (size <= 0) {
        free(interface);
        return NULL;
    }
    utf8 = xmalloc(size);
    WideCharToMultiByte(
        CP_UTF8,
        0,
        has_interface ? interface : caps->szPname,
        -1,
        utf8,
        size,
        NULL,
        NULL);
    identity = xmalloc(size + 96);
    if (has_interface) {
        str_format(identity, size + 96, "midi:interface:%s", utf8);
    } else {
        /* No port index: unrelated devices changing order must not unbind
           this port. Identical legacy ports need an occurrence suffix. */
        str_format(
            identity,
            size + 96,
            "midi:legacy:%04x:%04x:%u:%s",
            caps->wMid,
            caps->wPid,
            duplicate,
            utf8);
    }
    free(utf8);
    free(interface);
    return identity;
}

static void midi_close_device(struct midi_device *device)
{
    MMRESULT result;
    HMODULE module;

    EnterCriticalSection(&device->lock);
    device->enabled = false;
    midi_state_reset(&device->state);
    LeaveCriticalSection(&device->lock);
    if (device->handle == NULL) {
        return;
    }
    midiInStop(device->handle);
    midiInReset(device->handle);
    result = midiInClose(device->handle);
    if (result == MMSYSERR_NOERROR) {
        device->handle = NULL;
    } else {
        /* Retained callback state also needs its code to remain loaded if
           a consumer unloads geninput after a faulty driver's close fails. */
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            (const char *) (uintptr_t) midi_receive,
            &module);
        log_warning("MIDI close failed (%u)", (unsigned int) result);
    }
}

static struct midi_device *
midi_find_or_create(const char *identity, const MIDIINCAPSW *caps)
{
    struct midi_device *device;

    hid_mgr_lock();
    for (device = midi_devices; device != NULL; device = device->next) {
        if (_stricmp(device->identity, identity) == 0) {
            break;
        }
    }
    if (device == NULL) {
        device = xcalloc(sizeof(*device));
        device->hid.vptr = &midi_vtbl;
        device->identity = str_dup(identity);
        swprintf(
            device->name,
            sizeof(device->name) / sizeof(wchar_t),
            L"MIDI: %ls",
            caps->szPname);
        InitializeCriticalSection(&device->lock);
        midi_state_reset(&device->state);
        device->next = midi_devices;
        midi_devices = device;
        hid_stub_attach(hid_mgr_get_named_stub(identity), &device->hid);
    }
    hid_mgr_unlock();
    return device;
}

static void midi_scan(void)
{
    struct midi_device *device;
    MIDIINCAPSW *caps;
    bool *valid;
    UINT count;
    UINT i;
    UINT j;
    UINT duplicate;
    char *identity;
    MMRESULT result;

    for (device = midi_devices; device != NULL; device = device->next) {
        device->seen = false;
    }
    count = midiInGetNumDevs();
    if (count > 4096) {
        log_warning("Ignoring invalid MIDI input port count %u", count);
        count = 0;
    }
    caps = xcalloc((count + 1) * sizeof(*caps));
    valid = xcalloc((count + 1) * sizeof(*valid));
    for (i = 0; i < count; i++) {
        if (WaitForSingleObject(midi_stop, 0) == WAIT_OBJECT_0) {
            break;
        }
        valid[i] =
            midiInGetDevCapsW(i, &caps[i], sizeof(caps[i])) == MMSYSERR_NOERROR;
        caps[i].szPname[MAXPNAMELEN - 1] = 0;
        if (!valid[i]) {
            continue;
        }
        duplicate = 0;
        for (j = 0; j < i; j++) {
            if (valid[j] && caps[j].wMid == caps[i].wMid &&
                caps[j].wPid == caps[i].wPid &&
                wcscmp(caps[j].szPname, caps[i].szPname) == 0) {
                duplicate++;
            }
        }
        identity = midi_identity(i, &caps[i], duplicate);
        if (identity == NULL) {
            continue;
        }
        device = midi_find_or_create(identity, &caps[i]);
        free(identity);
        device->seen = true;
        if (device->handle != NULL && device->index != i) {
            midi_close_device(device);
        }
        if (device->handle != NULL) {
            continue;
        }
        device->index = i;
        result = midiInOpen(
            &device->handle,
            i,
            (DWORD_PTR) midi_receive,
            (DWORD_PTR) device,
            CALLBACK_FUNCTION | MIDI_IO_STATUS);
        if (result == MMSYSERR_NOERROR) {
            EnterCriticalSection(&device->lock);
            device->enabled = true;
            LeaveCriticalSection(&device->lock);
            result = midiInStart(device->handle);
            if (result != MMSYSERR_NOERROR) {
                midi_close_device(device);
            } else {
                log_info("MIDI input ready: %s", device->identity);
            }
        } else {
            device->handle = NULL;
        }
        if (result != MMSYSERR_NOERROR && device->last_error != result) {
            log_warning(
                "MIDI input unavailable (%u): %s",
                (unsigned int) result,
                device->identity);
        }
        device->last_error = result;
    }
    free(valid);
    free(caps);
    for (device = midi_devices; device != NULL; device = device->next) {
        if (!device->seen) {
            midi_close_device(device);
        }
    }
}

static DWORD WINAPI midi_worker(void *context)
{
    struct midi_device *device;

    do {
        midi_scan();
    } while (WaitForSingleObject(midi_stop, 2000) == WAIT_TIMEOUT);
    for (device = midi_devices; device != NULL; device = device->next) {
        midi_close_device(device);
    }
    return 0;
}

void midi_init(void)
{
    midi_devices = NULL;
    midi_thread = NULL;
    midi_stop = CreateEvent(NULL, TRUE, FALSE, NULL);
    midi_drum = false;
    midi_capture = false;
}

void midi_enable(const char *game_type)
{
    if (strcmp(game_type, "gf") != 0 && strcmp(game_type, "dm") != 0) {
        return;
    }
    hid_mgr_lock();
    midi_drum = strcmp(game_type, "dm") == 0;
    hid_mgr_unlock();
    if (midi_thread == NULL && midi_stop != NULL) {
        midi_thread = CreateThread(NULL, 0, midi_worker, NULL, 0, NULL);
        if (midi_thread == NULL) {
            log_warning("Could not start MIDI input worker");
        }
    }
}

void midi_poll(void)
{
    struct midi_device *device;
    uint64_t now = GetTickCount64();

    for (device = midi_devices; device != NULL; device = device->next) {
        EnterCriticalSection(&device->lock);
        midi_state_poll(&device->state, now, midi_drum, midi_capture);
        if (device->state.overflow) {
            log_warning("MIDI input queue overflow: %s", device->identity);
            device->state.overflow = false;
        }
        LeaveCriticalSection(&device->lock);
    }
}

void midi_set_capture(bool capture)
{
    struct midi_device *device;

    hid_mgr_lock();
    midi_capture = capture;
    for (device = midi_devices; device != NULL; device = device->next) {
        EnterCriticalSection(&device->lock);
        /* Old hits must not bind the next action in a continuous sequence. */
        memset(device->state.notes, 0, sizeof(device->state.notes));
        LeaveCriticalSection(&device->lock);
    }
    hid_mgr_unlock();
}

void midi_fini(void)
{
    struct midi_device *device;

    if (midi_thread != NULL) {
        SetEvent(midi_stop);
        WaitForSingleObject(midi_thread, INFINITE);
        CloseHandle(midi_thread);
        midi_thread = NULL;
    }
    for (device = midi_devices; device != NULL; device = device->next) {
        midi_close_device(device);
    }
    if (midi_stop != NULL) {
        CloseHandle(midi_stop);
        midi_stop = NULL;
    }
    /* HID manager owns the devices and frees them after the worker exits. */
    midi_devices = NULL;
}
