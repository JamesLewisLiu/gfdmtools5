#include <windows.h>

#include <stdbool.h>
#include <string.h>

#include "gfdmhook1/bios.h"

static bool gfdm_bios_query_date(
    const wchar_t *path, const wchar_t *name, char *date)
{
    HKEY key;
    wchar_t value[GFDM_BIOS_DATE_SIZE] = {0};
    DWORD size = sizeof(value);
    DWORD type = 0;
    LSTATUS status;
    size_t len;

    status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    status = RegQueryValueExW(
        key, name, NULL, &type, (BYTE *) value, &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ ||
        size < sizeof(wchar_t) || size > sizeof(value) ||
        size % sizeof(wchar_t) != 0) {
        return false;
    }

    /* V4 renders single-byte text. Reject malformed or non-printable values
       before copying anything; a failed query must never expose stack data. */
    for (len = 0; len < size / sizeof(wchar_t); len++) {
        if (value[len] == L'\0') {
            break;
        }
        if (value[len] < L' ' || value[len] > L'~') {
            return false;
        }
    }
    if (len == 0 || len == size / sizeof(wchar_t)) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        date[i] = (char) value[i];
    }
    date[len] = '\0';
    return true;
}

void gfdm_bios_read_date(char date[GFDM_BIOS_DATE_SIZE])
{
    memset(date, 0, GFDM_BIOS_DATE_SIZE);
    if (gfdm_bios_query_date(
            L"HARDWARE\\DESCRIPTION\\System", L"SystemBiosDate", date) ||
        gfdm_bios_query_date(
            L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSReleaseDate", date)) {
        return;
    }

    strcpy(date, "UNKNOWN");
}
