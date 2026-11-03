/* CardIO HID protocol adapter for stock EAMIO. Protocol behavior reviewed
   against spice2x external/cardio; this C worker/state integration is new.
   No Spice runner, logging, window or game code is incorporated. */
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
// clang-format on
#include "eamio/cardio.h"
#include "bemanitools/eamio.h"
#include "eamio/cardio-state.h"
#include "util/log.h"
#include "util/thread.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CARDIO_PAGE 0xffca
#define CARDIO_MAX_REPORT 4096
#define CARDIO_MAX_CAPS 64
struct cardio_device {
    wchar_t *path;
    HANDLE file;
    OVERLAPPED io;
    bool pending;
    PHIDP_PREPARSED_DATA data;
    HIDP_CAPS caps;
    HIDP_VALUE_CAPS values[CARDIO_MAX_CAPS];
    USHORT nvalues;
    uint8_t report[CARDIO_MAX_REPORT];
};
static struct cardio_device devices[2];
static struct cardio_state states[2];
static CRITICAL_SECTION state_lock;
static HANDLE stop_event;
static int worker;
static bool started;

static bool compatible_value(const HIDP_VALUE_CAPS *v)
{
    return v->UsagePage == CARDIO_PAGE && !v->IsRange &&
        (v->NotRange.Usage == 0x41 || v->NotRange.Usage == 0x42) &&
        v->BitSize == 8 && v->ReportCount == 8;
}

static void close_device(struct cardio_device *d)
{
    if (d->file != INVALID_HANDLE_VALUE) {
        if (d->pending) {
            DWORD ignored;
            CancelIoEx(d->file, &d->io);
            /* The worker owns both cancellation and completion. Keep the
               OVERLAPPED and buffer alive until the driver completes. */
            GetOverlappedResult(d->file, &d->io, &ignored, TRUE);
        }
        CloseHandle(d->file);
    }
    if (d->data)
        HidD_FreePreparsedData(d->data);
    if (d->io.hEvent)
        CloseHandle(d->io.hEvent);
    d->file = INVALID_HANDLE_VALUE;
    d->data = NULL;
    memset(&d->io, 0, sizeof(d->io));
    d->pending = false;
}

static bool open_device(struct cardio_device *d, const wchar_t *path)
{
    bool found = false;
    /* Query descriptors before asking for input access. No output writes. */
    HANDLE query = CreateFileW(
        path,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (query == INVALID_HANDLE_VALUE)
        return false;
    bool ok = HidD_GetPreparsedData(query, &d->data) &&
        HidP_GetCaps(d->data, &d->caps) == HIDP_STATUS_SUCCESS;
    CloseHandle(query);
    if (!ok || d->caps.UsagePage != CARDIO_PAGE ||
        d->caps.InputReportByteLength < 9 ||
        d->caps.InputReportByteLength > CARDIO_MAX_REPORT ||
        d->caps.NumberInputValueCaps == 0 ||
        d->caps.NumberInputValueCaps > CARDIO_MAX_CAPS)
        goto fail;
    d->nvalues = d->caps.NumberInputValueCaps;
    if (HidP_GetValueCaps(HidP_Input, d->values, &d->nvalues, d->data) !=
            HIDP_STATUS_SUCCESS ||
        d->nvalues > CARDIO_MAX_CAPS)
        goto fail;
    for (USHORT i = 0; i < d->nvalues; i++)
        found |= compatible_value(&d->values[i]);
    if (!found)
        goto fail;
    d->file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);
    if (d->file == INVALID_HANDLE_VALUE)
        goto fail;
    d->io.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!d->io.hEvent)
        goto fail;
    return true;
fail:
    close_device(d);
    return false;
}

static void scan_devices(void)
{
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO list = SetupDiGetClassDevsW(
        &guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (list == INVALID_HANDLE_VALUE)
        return;
    for (DWORD i = 0; WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0;
         i++) {
        SP_DEVICE_INTERFACE_DATA item = {0};
        DWORD size = 0;
        item.cbSize = sizeof(item);
        if (!SetupDiEnumDeviceInterfaces(list, NULL, &guid, i, &item))
            break;
        SetupDiGetDeviceInterfaceDetailW(list, &item, NULL, 0, &size, NULL);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536)
            continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = calloc(1, size);
        if (!detail)
            break;
        detail->cbSize = sizeof(*detail);
        if (SetupDiGetDeviceInterfaceDetailW(
                list, &item, detail, size, NULL, NULL)) {
            int slot = -1, empty = -1;
            for (int j = 0; j < 2; j++) {
                if (devices[j].path &&
                    !_wcsicmp(devices[j].path, detail->DevicePath)) {
                    slot = j;
                    break;
                }
                if (!devices[j].path && empty < 0)
                    empty = j;
            }
            if (slot < 0)
                slot = empty;
            if (slot >= 0 && devices[slot].file == INVALID_HANDLE_VALUE &&
                open_device(&devices[slot], detail->DevicePath)) {
                if (!devices[slot].path)
                    devices[slot].path = _wcsdup(detail->DevicePath);
                if (!devices[slot].path)
                    close_device(&devices[slot]);
                else
                    log_info("CardIO reader %d connected", slot + 1);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(list);
}

static bool decode_report(
    struct cardio_device *d, DWORD size, uint8_t uid[8], uint8_t *type)
{
    if (size != d->caps.InputReportByteLength || size < 9 ||
        size > sizeof(d->report) || d->nvalues > CARDIO_MAX_CAPS)
        return false;
    for (USHORT i = 0; i < d->nvalues; i++) {
        HIDP_VALUE_CAPS *v = &d->values[i];
        if (!compatible_value(v) || v->ReportID != d->report[0])
            continue;
        memset(uid, 0, 8);
        if (HidP_GetUsageValueArray(
                HidP_Input,
                CARDIO_PAGE,
                v->LinkCollection,
                v->NotRange.Usage,
                (PCHAR) uid,
                8,
                d->data,
                (PCHAR) d->report,
                size) != HIDP_STATUS_SUCCESS)
            return false;
        *type = v->NotRange.Usage == 0x41 ? EAM_IO_CARD_ISO15696 :
                                            EAM_IO_CARD_FELICA;
        return true;
    }
    return false;
}

static bool poll_device(struct cardio_device *d, int unit)
{
    DWORD size = 0;
    if (d->pending) {
        if (!GetOverlappedResult(d->file, &d->io, &size, FALSE)) {
            return GetLastError() == ERROR_IO_INCOMPLETE;
        }
        d->pending = false;
    } else {
        HANDLE event = d->io.hEvent;
        memset(&d->io, 0, sizeof(d->io));
        d->io.hEvent = event;
        ResetEvent(event);
        if (!ReadFile(
                d->file,
                d->report,
                d->caps.InputReportByteLength,
                &size,
                &d->io)) {
            if (GetLastError() != ERROR_IO_PENDING)
                return false;
            d->pending = true;
            return true;
        }
    }
    uint8_t uid[8], type;
    if (decode_report(d, size, uid, &type)) {
        EnterCriticalSection(&state_lock);
        bool accepted = cardio_state_offer(&states[unit], uid, type);
        LeaveCriticalSection(&state_lock);
        if (!accepted)
            log_warning(
                "CardIO reader %d event queue full; tap again", unit + 1);
    }
    return true;
}

static int cardio_worker(void *ctx)
{
    (void) ctx;
    DWORD scanned = GetTickCount() - 2000;
    while (WaitForSingleObject(stop_event, 10) == WAIT_TIMEOUT) {
        DWORD now = GetTickCount();
        if ((DWORD) (now - scanned) >= 2000) {
            scan_devices();
            scanned = now;
        }
        for (int i = 0; i < 2; i++) {
            if (devices[i].file != INVALID_HANDLE_VALUE &&
                !poll_device(&devices[i], i)) {
                close_device(&devices[i]);
                EnterCriticalSection(&state_lock);
                cardio_state_release(&states[i]);
                LeaveCriticalSection(&state_lock);
                log_info("CardIO reader %d disconnected", i + 1);
            }
        }
    }
    for (int i = 0; i < 2; i++)
        close_device(&devices[i]);
    return 0;
}

bool eam_io_cardio_start(void)
{
    if (started)
        return true;
    stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!stop_event)
        return false;
    InitializeCriticalSection(&state_lock);
    memset(states, 0, sizeof(states));
    memset(devices, 0, sizeof(devices));
    for (int i = 0; i < 2; i++)
        devices[i].file = INVALID_HANDLE_VALUE;
    worker = thread_create(cardio_worker, NULL, 0, 0);
    if (!worker || worker == -1) {
        CloseHandle(stop_event);
        stop_event = NULL;
        DeleteCriticalSection(&state_lock);
        return false;
    }
    started = true;
    log_info("CardIO enabled; scanning asynchronously for HID page 0xffca");
    return true;
}

void cardio_stop(void)
{
    if (!started)
        return;
    SetEvent(stop_event);
    thread_join(worker, NULL);
    thread_destroy(worker);
    for (int i = 0; i < 2; i++)
        free(devices[i].path);
    CloseHandle(stop_event);
    stop_event = NULL;
    DeleteCriticalSection(&state_lock);
    started = false;
}

uint8_t cardio_sensor(uint8_t unit, bool *owned)
{
    *owned = false;
    if (!started || unit >= 2)
        return 0;
    EnterCriticalSection(&state_lock);
    bool high = cardio_state_sensor(&states[unit], GetTickCount(), owned);
    LeaveCriticalSection(&state_lock);
    return high ? (1 << EAM_IO_SENSOR_FRONT) | (1 << EAM_IO_SENSOR_BACK) : 0;
}

uint8_t cardio_read(uint8_t unit, uint8_t *uid, uint8_t size)
{
    if (!started || unit >= 2)
        return EAM_IO_CARD_NONE;
    EnterCriticalSection(&state_lock);
    uint8_t type = cardio_state_read(&states[unit], uid, size);
    LeaveCriticalSection(&state_lock);
    return type;
}
