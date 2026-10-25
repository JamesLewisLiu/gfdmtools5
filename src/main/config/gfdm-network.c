#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "config/resource.h"
#include "config/schema.h"
#include "config/gfdm-network-config.h"

#include "util/defs.h"
#include "util/log.h"

struct gfdm_network_page {
    HWND hwnd;
    char mode[3];
    char server[256];
    char adapter[64];
    wchar_t path[MAX_PATH];
    bool keyboard;
    bool path_valid;
    unsigned int version;
};

static INT_PTR CALLBACK gfdm_network_dlg_proc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void gfdm_network_init_path(struct gfdm_network_page *page)
{
    wchar_t executable[MAX_PATH];
    DWORD size;

    size = GetModuleFileNameW(NULL, executable, lengthof(executable));
    if (size == 0 || size >= lengthof(executable)) {
        return;
    }
    page->path_valid = gfdm_network_select_path(executable, page->mode,
        page->path, lengthof(page->path), &page->version);
}

static void gfdm_network_load(struct gfdm_network_page *page)
{
    char line[512];
    FILE *f;

    strcpy(page->server, "localhost:80");
    page->adapter[0] = '\0';
    page->keyboard = false;
    if (!page->path_valid) {
        return;
    }
    f = _wfopen(page->path, L"rb");

    if (f == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "eamuse.server=", 14) == 0) {
            sscanf(line + 14, "%255[^\r\n]", page->server);
        } else if (strncmp(line, "adapter.override_ip=", 20) == 0) {
            page->adapter[0] = '\0';
            sscanf(line + 20, "%63[^\r\n]", page->adapter);
        } else if (strncmp(line, "input.keyboard=", 15) == 0) {
            char value[16] = {0};
            sscanf(line + 15, "%15s", value);
            page->keyboard = _stricmp(value, "true") == 0
                || strcmp(value, "1") == 0;
        }
    }

    fclose(f);
}

static void gfdm_network_save(struct gfdm_network_page *page)
{
    wchar_t tmp_path[MAX_PATH + 4];
    FILE *src;
    FILE *dst;

    if (!page->path_valid) {
        log_warning("Could not identify a unique GFDM configuration file");
        return;
    }

    GetDlgItemTextA(
        page->hwnd,
        IDC_GFDM_SERVER,
        page->server,
        sizeof(page->server));
    GetDlgItemTextA(
        page->hwnd,
        IDC_GFDM_ADAPTER,
        page->adapter,
        sizeof(page->adapter));

    page->keyboard = IsDlgButtonChecked(page->hwnd, IDC_GFDM_KEYBOARD) == BST_CHECKED;
    swprintf(tmp_path, lengthof(tmp_path), L"%ls.tmp", page->path);
    src = _wfopen(page->path, L"rb");
    dst = _wfopen(tmp_path, L"wb");

    if (dst == NULL) {
        log_warning("Could not write GFDM configuration");
        if (src != NULL) {
            fclose(src);
        }
        return;
    }

    if (src == NULL && GetFileAttributesW(page->path) != INVALID_FILE_ATTRIBUTES) {
        log_warning("Could not read existing GFDM configuration");
        fclose(dst);
        DeleteFileW(tmp_path);
        return;
    }

    if (!gfdm_network_write_config(src, dst, page->version, page->mode,
            page->server, page->adapter, page->keyboard)) {
        log_warning("Could not write GFDM configuration contents");
        if (src != NULL) {
            fclose(src);
        }
        fclose(dst);
        DeleteFileW(tmp_path);
        return;
    }
    if (src != NULL) {
        fclose(src);
    }

    if (fclose(dst) != 0 || !MoveFileExW(tmp_path, page->path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log_warning("Could not save GFDM configuration");
    }
}

HPROPSHEETPAGE gfdm_network_tab_create(
    HINSTANCE inst, const struct schema *schema)
{
    PROPSHEETPAGE psp;

    memset(&psp, 0, sizeof(psp));
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = inst;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_TAB_GFDM_NETWORK);
    psp.pfnDlgProc = gfdm_network_dlg_proc;
    psp.lParam = (LPARAM) schema;

    return CreatePropertySheetPage(&psp);
}

static INT_PTR CALLBACK gfdm_network_dlg_proc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct gfdm_network_page *page;
    const PROPSHEETPAGE *psp;

    (void) wparam;

    page = (struct gfdm_network_page *) GetWindowLongPtr(
        hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_INITDIALOG:
            psp = (const PROPSHEETPAGE *) lparam;
            page = calloc(1, sizeof(*page));
            if (page == NULL) {
                return FALSE;
            }
            strcpy(page->mode, psp->lParam != 0 &&
                                  strcmp(((const struct schema *) psp->lParam)->name,
                                         "gf") == 0
                              ? "gf"
                              : "dm");
            page->hwnd = hwnd;
            gfdm_network_init_path(page);
            gfdm_network_load(page);
            if (!page->path_valid) {
                EnableWindow(GetDlgItem(hwnd, IDC_GFDM_SERVER), FALSE);
                EnableWindow(GetDlgItem(hwnd, IDC_GFDM_ADAPTER), FALSE);
                EnableWindow(GetDlgItem(hwnd, IDC_GFDM_KEYBOARD), FALSE);
            }
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) page);
            SetDlgItemTextA(hwnd, IDC_GFDM_SERVER, page->server);
            SetDlgItemTextA(hwnd, IDC_GFDM_ADAPTER, page->adapter);
            CheckDlgButton(hwnd, IDC_GFDM_KEYBOARD,
                page->keyboard ? BST_CHECKED : BST_UNCHECKED);
            if (page->path_valid) {
                SetDlgItemTextW(hwnd, IDC_GFDM_CONFIG_PATH,
                    wcsrchr(page->path, L'\\') != NULL
                        ? wcsrchr(page->path, L'\\') + 1 : page->path);
            } else {
                SetDlgItemTextW(hwnd, IDC_GFDM_CONFIG_PATH,
                    L"Cannot select a version. Keep one version's .conf files beside config.exe.");
            }
            return TRUE;

        case WM_NOTIFY:
            if (((const NMHDR *) lparam)->code == PSN_KILLACTIVE) {
                gfdm_network_save(page);
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            }
            return FALSE;

        case WM_DESTROY:
            free(page);
            return TRUE;

        default:
            return FALSE;
    }
}
