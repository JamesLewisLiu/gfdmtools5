#include "util/log-file.h"
#include <string.h>

bool log_file_option(
    int argc, char **argv, int *index, char *path, size_t capacity)
{
    const char *name = "log.log";
    if (*index + 1 < argc && argv[*index + 1][0] != '-') {
        name = argv[++*index];
    }
    if (!name[0] || strlen(name) >= capacity)
        return false;
    strcpy(path, name);
    return true;
}

HANDLE log_file_open(const char *path, bool truncate)
{
    HANDLE file;
    if (!path || !path[0])
        return INVALID_HANDLE_VALUE;
    if (truncate) {
        file = CreateFileA(
            path,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (file == INVALID_HANDLE_VALUE)
            return file;
        CloseHandle(file);
    }
    return CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
}

void log_file_append(HANDLE file, const char *text, size_t length)
{
    DWORD written;
    if (file && file != INVALID_HANDLE_VALUE && length && length <= MAXDWORD) {
        /* One append operation keeps records from inject and its child
           together without shared C-runtime file-position buffers. */
        WriteFile(file, text, (DWORD) length, &written, NULL);
    }
}
