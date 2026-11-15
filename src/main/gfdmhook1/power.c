#include <windows.h>

#include "gfdmhook1/power.h"
#include "util/log.h"

static HANDLE power_request;

void gfdm_power_init(void)
{
    HANDLE(WINAPI * create_request)(PREASON_CONTEXT);
    BOOL(WINAPI * set_request)(HANDLE, POWER_REQUEST_TYPE);
    REASON_CONTEXT reason = {0};
    HMODULE kernel;
    HANDLE request;
    DWORD error;

    /* Called once from game initialization, outside DllMain. A request object
       is process-owned: unlike thread execution state, it survives return
       from boot_main or replacement of a rendering/input thread. Keep it for
       the whole game process, including TEST, autoplay and background play. */
    if (power_request != NULL)
        return;
    kernel = GetModuleHandleW(L"kernel32.dll");
    create_request = (void *) GetProcAddress(kernel, "PowerCreateRequest");
    set_request = (void *) GetProcAddress(kernel, "PowerSetRequest");
    if (create_request == NULL || set_request == NULL) {
        log_warning(
            "GFDM power: display keep-awake requires Windows 7 or later");
        return;
    }
    reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    reason.Reason.SimpleReasonString = L"GFDM game is running";
    request = create_request(&reason);
    if (request == INVALID_HANDLE_VALUE) {
        log_warning(
            "GFDM power: request creation failed (error %lu)", GetLastError());
        return;
    }
    /* Microsoft requires SystemRequired as well as DisplayRequired to keep
       the display on without allowing an idle system sleep. No away mode,
       input injection or permanent power-plan changes are needed. */
    if (!set_request(request, PowerRequestSystemRequired) ||
        !set_request(request, PowerRequestDisplayRequired)) {
        error = GetLastError();
        CloseHandle(
            request); /* Destroy the object, including partial requests. */
        log_warning("GFDM power: keep-awake request failed (error %lu)", error);
        return;
    }
    power_request = request;
    log_info(
        "GFDM power: display and system idle sleep inhibited while running");
}

void gfdm_power_fini(void)
{
    /* Only close an owned kernel handle during DLL detach; do not load DLLs,
       wait for workers or call application logging under the loader lock.
       Windows also closes this handle on crash/forced process termination. */
    HANDLE request = InterlockedExchangePointer(&power_request, NULL);
    if (request != NULL)
        CloseHandle(request);
}
