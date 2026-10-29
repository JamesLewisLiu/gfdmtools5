#ifndef UTIL_LOG_FILE_H
#define UTIL_LOG_FILE_H
#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

bool log_file_option(
    int argc, char **argv, int *index, char *path, size_t capacity);
HANDLE log_file_open(const char *path, bool truncate);
void log_file_append(HANDLE file, const char *text, size_t length);
#endif
