#include "gfdmhook1/logging.h"
#include "util/cmdline.h"
#include "util/log-file.h"
#include <stdbool.h>
#include <string.h>
#include <windows.h>

static INIT_ONCE logging_once = INIT_ONCE_STATIC_INIT;
static HANDLE logging_console;
static HANDLE logging_file = INVALID_HANDLE_VALUE;
static bool logging_debugger;
static CRITICAL_SECTION logging_lock;

static BOOL CALLBACK
logging_setup(INIT_ONCE *once, void *parameter, void **context)
{
    int argc;
    char **argv;
    char path[MAX_PATH] = "";
    bool debug = false, remote = false, valid = true;
    InitializeCriticalSection(&logging_lock);
    args_recover(&argc, &argv);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-D"))
            debug = true;
        else if (!strcmp(argv[i], "-R"))
            remote = true;
        else if (!strcmp(argv[i], "-Y")) {
            if (!log_file_option(argc, argv, &i, path, sizeof(path)))
                valid = false;
        }
    }
    args_free(argc, argv);
    logging_debugger = debug && !remote;
    if (!logging_debugger) {
        /* GUI executables need not initially belong to inject's console. */
        AttachConsole(ATTACH_PARENT_PROCESS);
        logging_console = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!logging_console || logging_console == INVALID_HANDLE_VALUE) {
            logging_console = CreateFileA(
                "CONOUT$",
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                0,
                NULL);
        }
        if (valid && path[0])
            logging_file = log_file_open(path, false);
    }
    return TRUE;
}

void gfdm_logging_init(void)
{
    InitOnceExecuteOnce(&logging_once, logging_setup, NULL, NULL);
}

static void logging_debug(const char *text, size_t length)
{
    /* AVS supplies an explicit length; it need not supply a NUL terminator. */
    char buffer[4096];
    while (length) {
        size_t count =
            length < sizeof(buffer) - 1 ? length : sizeof(buffer) - 1;
        memcpy(buffer, text, count);
        buffer[count] = '\0';
        OutputDebugStringA(buffer);
        text += count;
        length -= count;
    }
}

void gfdm_log_writer(void *context, const char *text, size_t length)
{
    gfdm_logging_init();
    EnterCriticalSection(&logging_lock);
    /* Keep debugger observers working. With -D, inject forwards this event
       to both destinations, so writing here as well would duplicate it. */
    logging_debug(text, length);
    if (!logging_debugger) {
        log_file_append(logging_console, text, length);
        log_file_append(logging_file, text, length);
    }
    LeaveCriticalSection(&logging_lock);
}
