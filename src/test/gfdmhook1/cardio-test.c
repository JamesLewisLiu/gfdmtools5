#undef NDEBUG
#define LOG_SUPPRESS
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
// clang-format on
#include "eamio/cardio-state.c"
#include "util/thread.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static volatile LONG scans;
static unsigned int creates, threads, cancels, completions;
static bool fail_thread, pending_read, complete_read, bad_caps;
static uint8_t fake_uid[8] = {0xe0, 4, 1, 2, 3, 4, 5, 6};
static HANDLE fake_file = (HANDLE) (uintptr_t) 0xdead001;

static HDEVINFO WINAPI
fake_scan(const GUID *guid, PCWSTR name, HWND hwnd, DWORD flags)
{
    (void) guid;
    (void) name;
    (void) hwnd;
    (void) flags;
    InterlockedIncrement(&scans);
    return INVALID_HANDLE_VALUE;
}
static int fake_thread(
    int (*proc)(void *), void *ctx, uint32_t stack, unsigned int priority)
{
    threads++;
    return fail_thread ? 0 : crt_thread_create(proc, ctx, stack, priority);
}
static HANDLE WINAPI fake_open(
    LPCWSTR path,
    DWORD access,
    DWORD share,
    LPSECURITY_ATTRIBUTES security,
    DWORD creation,
    DWORD flags,
    HANDLE template)
{
    (void) path;
    (void) share;
    (void) security;
    (void) creation;
    (void) flags;
    (void) template;
    assert(access == 0 || access == GENERIC_READ);
    creates++;
    return fake_file;
}
static BOOL WINAPI fake_close(HANDLE file)
{
    return file == fake_file ? TRUE : CloseHandle(file);
}
static BOOLEAN WINAPI fake_preparsed(HANDLE file, PHIDP_PREPARSED_DATA *data)
{
    assert(file == fake_file);
    *data = (PHIDP_PREPARSED_DATA) (uintptr_t) 1;
    return TRUE;
}
static BOOLEAN WINAPI fake_free(PHIDP_PREPARSED_DATA data)
{
    (void) data;
    return TRUE;
}
static NTSTATUS WINAPI fake_caps(PHIDP_PREPARSED_DATA data, PHIDP_CAPS caps)
{
    (void) data;
    memset(caps, 0, sizeof(*caps));
    caps->UsagePage = bad_caps ? 1 : 0xffca;
    caps->InputReportByteLength = 9;
    caps->NumberInputValueCaps = 1;
    return HIDP_STATUS_SUCCESS;
}
static NTSTATUS WINAPI fake_values(
    HIDP_REPORT_TYPE type,
    PHIDP_VALUE_CAPS values,
    PUSHORT count,
    PHIDP_PREPARSED_DATA data)
{
    (void) type;
    (void) data;
    assert(*count >= 1);
    *count = 1;
    memset(values, 0, sizeof(*values));
    values->UsagePage = 0xffca;
    values->NotRange.Usage = 0x41;
    values->BitSize = 8;
    values->ReportCount = 8;
    values->LinkCollection = 7;
    return HIDP_STATUS_SUCCESS;
}
static NTSTATUS WINAPI fake_array(
    HIDP_REPORT_TYPE type,
    USAGE page,
    USHORT collection,
    USAGE usage,
    PCHAR value,
    USHORT size,
    PHIDP_PREPARSED_DATA data,
    PCHAR report,
    ULONG length)
{
    (void) type;
    (void) data;
    (void) report;
    assert(
        page == 0xffca && collection == 7 && (usage == 0x41 || usage == 0x42));
    assert(size == 8 && length == 9);
    memcpy(value, fake_uid, 8);
    return HIDP_STATUS_SUCCESS;
}
static BOOL WINAPI
fake_read(HANDLE file, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED io)
{
    (void) io;
    assert(file == fake_file && size == 9);
    memset(buffer, 0, size);
    *read = 9;
    if (pending_read) {
        SetLastError(ERROR_IO_PENDING);
        return FALSE;
    }
    return TRUE;
}
static BOOL WINAPI
fake_result(HANDLE file, LPOVERLAPPED io, LPDWORD size, BOOL wait)
{
    (void) io;
    assert(file == fake_file);
    completions++;
    if (!wait && !complete_read) {
        SetLastError(ERROR_IO_INCOMPLETE);
        return FALSE;
    }
    *size = 9;
    return TRUE;
}
static BOOL WINAPI fake_cancel(HANDLE file, LPOVERLAPPED io)
{
    (void) io;
    assert(file == fake_file);
    cancels++;
    return TRUE;
}
#define SetupDiGetClassDevsW fake_scan
#define thread_create fake_thread
#define CreateFileW fake_open
#define CloseHandle fake_close
#define HidD_GetPreparsedData fake_preparsed
#define HidD_FreePreparsedData fake_free
#define HidP_GetCaps fake_caps
#define HidP_GetValueCaps fake_values
#define HidP_GetUsageValueArray fake_array
#define ReadFile fake_read
#define GetOverlappedResult fake_result
#define CancelIoEx fake_cancel
#include "eamio/cardio.c"
#undef thread_create
#undef CloseHandle

static void check_state(void)
{
    struct cardio_state s = {0};
    uint8_t uid[8], zero[8] = {0};
    bool owned;
    assert(!cardio_state_sensor(&s, 0, &owned) && !owned);
    assert(!cardio_state_offer(&s, fake_uid, 0x41));
    assert(cardio_state_offer(&s, fake_uid, EAM_IO_CARD_ISO15696));
    for (int i = 0; i < 100; i++)
        assert(cardio_state_offer(&s, fake_uid, EAM_IO_CARD_ISO15696));
    assert(s.count == 1);
    assert(!cardio_state_sensor(&s, 0, &owned) && owned);
    assert(!cardio_state_sensor(&s, 49, &owned));
    assert(cardio_state_sensor(&s, 50, &owned));
    assert(cardio_state_sensor(
        &s, 10000, &owned)); /* Not consumed: UID stays available. */
    assert(cardio_state_read(&s, uid, 7) == 0);
    assert(cardio_state_read(&s, NULL, 8) == 0);
    assert(
        cardio_state_read(&s, uid, 8) == EAM_IO_CARD_ISO15696 &&
        !memcmp(uid, fake_uid, 8));
    assert(!cardio_state_sensor(&s, 10001, &owned) && owned);
    assert(!cardio_state_sensor(&s, 10050, &owned) && owned);
    assert(!cardio_state_sensor(&s, 10051, &owned) && !owned);
    assert(cardio_state_offer(&s, zero, EAM_IO_CARD_ISO15696));
    assert(cardio_state_offer(&s, fake_uid, EAM_IO_CARD_FELICA));
    assert(!cardio_state_sensor(&s, UINT32_MAX - 20, &owned));
    assert(cardio_state_sensor(&s, 40, &owned)); /* Timer wrap. */
    assert(cardio_state_read(&s, uid, 8) == EAM_IO_CARD_FELICA);
    assert(cardio_state_sensor(&s, 289, &owned));
    assert(!cardio_state_sensor(&s, 290, &owned));
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < CARDIO_QUEUE_SIZE; i++) {
        uint8_t card[8] = {1, (uint8_t) i};
        assert(cardio_state_offer(&s, card, 1));
    }
    assert(!cardio_state_offer(&s, fake_uid, 1));
    for (int i = 0; i < CARDIO_QUEUE_SIZE; i++) {
        uint32_t now = i * 500;
        assert(!cardio_state_sensor(&s, now, &owned));
        assert(cardio_state_sensor(&s, now + 50, &owned));
        assert(cardio_state_read(&s, uid, 8) == 1 && uid[1] == i);
        assert(!cardio_state_sensor(&s, now + 300, &owned));
    }
    puts(
        "PASS: UID/type, held deduplication, zero/re-tap, changed cards, "
        "bounded ordered queue, sensor/read snapshot, low gaps and timer wrap");
}

int main(void)
{
    bool owned;
    uint8_t uid[8], type;
    check_state();
    assert(cardio_sensor(0, &owned) == 0 && !owned);
    assert(cardio_read(0, uid, 8) == 0);
    cardio_stop();
    assert(scans == 0 && creates == 0 && threads == 0);
    fail_thread = true;
    assert(!eam_io_cardio_start());
    assert(!started && stop_event == NULL && scans == 0);
    fail_thread = false;
    assert(eam_io_cardio_start());
    int id = worker;
    assert(eam_io_cardio_start() && id == worker && threads == 2);
    DWORD began = GetTickCount();
    while (!InterlockedCompareExchange(&scans, 0, 0) &&
           GetTickCount() - began < 2000)
        Sleep(1);
    assert(
        scans > 0 && creates == 0); /* Worker scan only, no real device I/O. */
    struct cardio_device d = {0};
    d.file = INVALID_HANDLE_VALUE;
    bad_caps = true;
    assert(!open_device(&d, L"fixture") && d.file == INVALID_HANDLE_VALUE);
    bad_caps = false;
    assert(open_device(&d, L"fixture"));
    memset(d.report, 0, 9);
    assert(!decode_report(&d, 8, uid, &type));
    assert(
        decode_report(&d, 9, uid, &type) && type == 1 &&
        !memcmp(uid, fake_uid, 8));
    d.values[0].NotRange.Usage = 0x42;
    assert(decode_report(&d, 9, uid, &type) && type == 2);
    d.values[0].ReportID = 2;
    assert(!decode_report(&d, 9, uid, &type));
    d.report[0] = 2;
    assert(decode_report(&d, 9, uid, &type));
    d.values[0].IsRange = TRUE;
    assert(!decode_report(&d, 9, uid, &type));
    d.values[0].IsRange = FALSE;
    d.values[0].ReportCount = 7;
    assert(!decode_report(&d, 9, uid, &type));
    d.values[0].ReportCount = 8;
    d.values[0].ReportID = 0;
    pending_read = true;
    assert(poll_device(&d, 0) && d.pending);
    unsigned before = completions;
    assert(poll_device(&d, 0) && d.pending && completions == before + 1);
    complete_read = true;
    assert(poll_device(&d, 0) && !d.pending);
    assert(states[0].count == 1 && states[1].count == 0);
    assert(poll_device(&d, 0) && d.pending);
    close_device(&d);
    assert(cancels == 1 && !d.pending && d.data == NULL);
    cardio_stop();
    cardio_stop();
    assert(!started && stop_event == NULL);
    assert(eam_io_cardio_start());
    cardio_stop();
    puts(
        "PASS: opt-in zero-work disabled, start failure/idempotence/restart, "
        "async no-device worker, descriptor/type/report validation, player "
        "isolation, pending read and cancel/join cleanup");
    return 0;
}
