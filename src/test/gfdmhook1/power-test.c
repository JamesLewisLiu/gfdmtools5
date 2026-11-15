#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "util/log.h"
static void test_warning(const char *format, ...)
{
    (void) format;
}
#undef log_warning
#define log_warning test_warning

static FARPROC test_export(HMODULE module, const char *name);
static BOOL WINAPI test_close(HANDLE handle);
#define GetProcAddress test_export
#define CloseHandle test_close
#include "gfdmhook1/power.c"
#undef GetProcAddress
#undef CloseHandle

static bool native, missing_create, missing_set, fail_create;
static int fail_set, creates, sets, closes;
static POWER_REQUEST_TYPE requests[2];

static HANDLE WINAPI fake_create(PREASON_CONTEXT reason)
{
    assert(reason->Version == POWER_REQUEST_CONTEXT_VERSION);
    assert(reason->Flags == POWER_REQUEST_CONTEXT_SIMPLE_STRING);
    assert(!wcscmp(reason->Reason.SimpleReasonString, L"GFDM game is running"));
    creates++;
    return fail_create ? INVALID_HANDLE_VALUE : (HANDLE) (uintptr_t) 1234;
}

static BOOL WINAPI fake_set(HANDLE handle, POWER_REQUEST_TYPE type)
{
    assert(handle == (HANDLE) (uintptr_t) 1234);
    assert(sets < 2);
    requests[sets++] = type;
    return sets != fail_set;
}

static FARPROC test_export(HMODULE module, const char *name)
{
    assert(module == GetModuleHandleW(L"kernel32.dll"));
    if (native)
        return GetProcAddress(module, name);
    if (!strcmp(name, "PowerCreateRequest"))
        return missing_create ? NULL : (FARPROC) (void *) fake_create;
    assert(!strcmp(name, "PowerSetRequest"));
    return missing_set ? NULL : (FARPROC) (void *) fake_set;
}

static BOOL WINAPI test_close(HANDLE handle)
{
    if (native)
        return CloseHandle(handle);
    assert(handle == (HANDLE) (uintptr_t) 1234);
    closes++;
    return TRUE;
}

static void reset(void)
{
    assert(power_request == NULL);
    missing_create = missing_set = fail_create = false;
    fail_set = creates = sets = closes = 0;
}

static DWORD WINAPI native_init(void *unused)
{
    (void) unused;
    gfdm_power_init();
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE request, thread;
    DWORD flags;
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "--native")));
    reset();
    gfdm_power_fini();
    assert(closes == 0);
    missing_create = true;
    gfdm_power_init();
    assert(creates == 0 && power_request == NULL);
    reset();
    missing_set = true;
    gfdm_power_init();
    assert(creates == 0 && power_request == NULL);
    reset();
    fail_create = true;
    gfdm_power_init();
    assert(creates == 1 && sets == 0 && closes == 0 && power_request == NULL);
    for (int fail = 1; fail <= 2; fail++) {
        reset();
        fail_set = fail;
        gfdm_power_init();
        assert(creates == 1 && sets == fail && closes == 1);
        assert(power_request == NULL);
        gfdm_power_fini();
        assert(closes == 1);
    }
    for (int cycle = 0; cycle < 3; cycle++) {
        reset();
        gfdm_power_init();
        assert(creates == 1 && sets == 2 && closes == 0);
        assert(requests[0] == PowerRequestSystemRequired);
        assert(requests[1] == PowerRequestDisplayRequired);
        gfdm_power_init();
        assert(creates == 1 && sets == 2); /* No reference count leak. */
        gfdm_power_fini();
        gfdm_power_fini();
        assert(closes == 1 && power_request == NULL);
    }
    puts(
        "PASS power: paired requests, failures/partial rollback, repeated init "
        "and cleanup");
    if (argc == 2) {
        native = true;
        thread = CreateThread(NULL, 0, native_init, NULL, 0, NULL);
        assert(thread != NULL);
        assert(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
        assert(CloseHandle(thread));
        request = power_request;
        assert(request != NULL && GetHandleInformation(request, &flags));
        gfdm_power_init();
        assert(power_request == request);
        gfdm_power_fini();
        assert(power_request == NULL);
        assert(!GetHandleInformation(request, &flags));
        assert(GetLastError() == ERROR_INVALID_HANDLE);
        gfdm_power_fini();
        puts(
            "PASS native power: request survives creating thread exit; cleanup "
            "closes kernel object");
    }
    return 0;
}
