#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "gfdmhook1/logging.c"

static DWORD WINAPI emit_thread(void *parameter)
{
    char line[80];
    for (unsigned i = 0; i < 20; i++) {
        int n = snprintf(
            line,
            sizeof(line),
            "I:fixture: worker-%u-%u\n",
            (unsigned) (uintptr_t) parameter,
            i);
        gfdm_log_writer(NULL, line, n);
    }
    return 0;
}

void fixture_emit(void)
{
    /* Exercise an AVS-style callback with no trailing NUL. */
    static const char raw[] = {'A', 'V', 'S', '-', 'R', 'A', 'W', '\n'};
    gfdm_log_writer(NULL, raw, sizeof(raw));
    HANDLE threads[4];
    for (unsigned i = 0; i < 4; i++) {
        threads[i] =
            CreateThread(NULL, 0, emit_thread, (void *) (uintptr_t) i, 0, NULL);
        assert(threads[i]);
    }
    assert(WaitForMultipleObjects(4, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 4; i++)
        CloseHandle(threads[i]);
    const char *line = "I:fixture: application-complete\n";
    gfdm_log_writer(NULL, line, strlen(line));
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        const char *line = "I:fixture: dll-attach\n";
        gfdm_log_writer(NULL, line, strlen(line));
    }
    return TRUE;
}
