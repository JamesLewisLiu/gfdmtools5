/* Only config.exe uses this adapter. Keep geninput's device identities,
 * reports and game-side behavior unchanged, while avoiding synchronous USB
 * string requests that can each stall for several seconds. */
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <initguid.h>
#include <devpkey.h>
// clang-format on

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "config/hid-metadata.h"
#include "hook/table.h"
#include "util/defs.h"

struct device_metadata {
    struct device_metadata *next;
    HANDLE handle;
    wchar_t product[256];
    wchar_t manufacturer[256];
};

static CRITICAL_SECTION metadata_lock;
static struct device_metadata *metadata;

static bool bus_description(
    HDEVINFO set, SP_DEVINFO_DATA *device, wchar_t *text, DWORD bytes)
{
    DEVPROPTYPE type;
    BOOL(WINAPI * get_property)(
        HDEVINFO,
        PSP_DEVINFO_DATA,
        const DEVPROPKEY *,
        DEVPROPTYPE *,
        PBYTE,
        DWORD,
        PDWORD,
        DWORD);
    get_property = (void *) GetProcAddress(
        GetModuleHandleA("setupapi.dll"), "SetupDiGetDevicePropertyW");
    if (get_property == NULL)
        return false;
    bool ok = get_property(
        set,
        device,
        &DEVPKEY_Device_BusReportedDeviceDesc,
        &type,
        (BYTE *) text,
        bytes,
        NULL,
        0);
    if (!ok || type != DEVPROP_TYPE_STRING) {
        text[0] = L'\0';
        return false;
    }
    text[bytes / sizeof(*text) - 1] = L'\0';
    return text[0] != L'\0';
}

static bool device_property(
    HDEVINFO set,
    SP_DEVINFO_DATA *device,
    DWORD property,
    wchar_t *text,
    DWORD bytes)
{
    DWORD type;
    bool ok = SetupDiGetDeviceRegistryPropertyW(
        set, device, property, &type, (BYTE *) text, bytes, NULL);
    if (!ok || type != REG_SZ) {
        text[0] = L'\0';
        return false;
    }
    text[bytes / sizeof(*text) - 1] = L'\0';
    return text[0] != L'\0';
}

static void cache_device(HANDLE handle, const char *path)
{
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA interface = {.cbSize = sizeof(interface)};
    SP_DEVINFO_DATA device = {.cbSize = sizeof(device)};
    SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail;
    struct device_metadata *entry;
    DWORD bytes = 0;

    if (handle == INVALID_HANDLE_VALUE || path == NULL ||
        strncmp(path, "\\\\?\\", 4) != 0)
        return;

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
        return;
    entry->handle = handle;
    /* A stable path is preferable to an unnamed device if registry lookup
       fails. It is display text only; mapper identity still uses geninput. */
    MultiByteToWideChar(
        CP_ACP, 0, path, -1, entry->product, lengthof(entry->product));
    entry->product[lengthof(entry->product) - 1] = L'\0';
    if (entry->product[0] == L'\0')
        wcscpy(entry->product, L"HID device");

    set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (set != INVALID_HANDLE_VALUE) {
        if (SetupDiOpenDeviceInterfaceA(set, path, 0, &interface)) {
            SetupDiGetDeviceInterfaceDetailA(
                set, &interface, NULL, 0, &bytes, NULL);
            detail = bytes >= sizeof(*detail) ? malloc(bytes) : NULL;
            if (detail != NULL) {
                detail->cbSize = sizeof(*detail);
                if (SetupDiGetDeviceInterfaceDetailA(
                        set, &interface, detail, bytes, NULL, &device)) {
                    wchar_t name[256];
                    if (bus_description(set, &device, name, sizeof(name)) ||
                        device_property(
                            set,
                            &device,
                            SPDRP_FRIENDLYNAME,
                            name,
                            sizeof(name)) ||
                        device_property(
                            set, &device, SPDRP_DEVICEDESC, name, sizeof(name)))
                        wcscpy(entry->product, name);
                    device_property(
                        set,
                        &device,
                        SPDRP_MFG,
                        entry->manufacturer,
                        sizeof(entry->manufacturer));
                }
                free(detail);
            }
        }
        SetupDiDestroyDeviceInfoList(set);
    }
    EnterCriticalSection(&metadata_lock);
    entry->next = metadata;
    metadata = entry;
    LeaveCriticalSection(&metadata_lock);
}

static HANDLE WINAPI metadata_open(
    LPCSTR path,
    DWORD access,
    DWORD share,
    LPSECURITY_ATTRIBUTES security,
    DWORD disposition,
    DWORD flags,
    HANDLE template)
{
    HANDLE handle = CreateFileA(
        path, access, share, security, disposition, flags, template);
    DWORD error = GetLastError();
    cache_device(handle, path);
    SetLastError(error);
    return handle;
}

static BOOL WINAPI metadata_close(HANDLE handle)
{
    struct device_metadata **cursor, *entry;
    DWORD error = GetLastError();
    EnterCriticalSection(&metadata_lock);
    for (cursor = &metadata; *cursor != NULL; cursor = &(*cursor)->next) {
        if ((*cursor)->handle == handle) {
            entry = *cursor;
            *cursor = entry->next;
            free(entry);
            break;
        }
    }
    LeaveCriticalSection(&metadata_lock);
    SetLastError(error);
    return CloseHandle(handle);
}

static BOOLEAN
metadata_string(HANDLE handle, PVOID buffer, ULONG bytes, bool manufacturer)
{
    struct device_metadata *entry;
    BOOLEAN result = FALSE;
    DWORD error = ERROR_NOT_FOUND;
    EnterCriticalSection(&metadata_lock);
    for (entry = metadata; entry != NULL; entry = entry->next) {
        if (entry->handle == handle) {
            const wchar_t *text =
                manufacturer ? entry->manufacturer : entry->product;
            size_t needed = (wcslen(text) + 1) * sizeof(*text);
            if (buffer != NULL && bytes >= needed && text[0] != L'\0') {
                memcpy(buffer, text, needed);
                result = TRUE;
            } else {
                error = ERROR_INSUFFICIENT_BUFFER;
            }
            break;
        }
    }
    LeaveCriticalSection(&metadata_lock);
    if (!result)
        SetLastError(error);
    return result;
}

static BOOLEAN WINAPI metadata_product(HANDLE handle, PVOID buffer, ULONG bytes)
{
    return metadata_string(handle, buffer, bytes, false);
}

static BOOLEAN WINAPI
metadata_manufacturer(HANDLE handle, PVOID buffer, ULONG bytes)
{
    return metadata_string(handle, buffer, bytes, true);
}

void config_hid_metadata_init(void)
{
    HMODULE input = GetModuleHandleA("geninput.dll");
    static const struct hook_symbol files[] = {
        {.name = "CreateFileA", .patch = metadata_open},
        {.name = "CloseHandle", .patch = metadata_close},
    };
    static const struct hook_symbol strings[] = {
        {.name = "HidD_GetProductString", .patch = metadata_product},
        {.name = "HidD_GetManufacturerString", .patch = metadata_manufacturer},
    };
    if (input == NULL)
        return;
    InitializeCriticalSection(&metadata_lock);
    hook_table_apply(input, "kernel32.dll", files, lengthof(files));
    hook_table_apply(input, "hid.dll", strings, lengthof(strings));
}
