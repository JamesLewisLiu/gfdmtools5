#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "config/resource.h"
#include "config/schema.h"

#include "util/defs.h"
#include "util/log.h"

struct gfdm_network_page {
    HWND hwnd;
    char mode[3];
    char server[256];
    char adapter[64];
    wchar_t path[MAX_PATH];
    bool keyboard;
};

static INT_PTR CALLBACK gfdm_network_dlg_proc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void gfdm_network_init_path(struct gfdm_network_page *page)
{
    wchar_t *name;
    unsigned int version = 4;

    GetModuleFileNameW(NULL, page->path, lengthof(page->path));
    name = wcsrchr(page->path, L'\\');
    if (name == NULL) {
        name = page->path;
    } else {
        name++;
    }
    for (unsigned int v = 4; v <= 8; v++) {
        swprintf(name, lengthof(page->path) - (name - page->path),
            L"gdv%u.exe", v);
        if (GetFileAttributesW(page->path) != INVALID_FILE_ATTRIBUTES) {
            version = v;
            break;
        }
    }
    swprintf(name, lengthof(page->path) - (name - page->path),
        L"gfdm-v%u-%hs.conf", version, page->mode);
}

static void gfdm_network_load(struct gfdm_network_page *page)
{
    char line[512];
    FILE *f;

    strcpy(page->server, "localhost:80");
    page->adapter[0] = '\0';
    page->keyboard = false;
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
    char line[512];
    FILE *src;
    FILE *dst;
    bool server_seen;
    bool adapter_seen;
    bool keyboard_seen;
    bool needs_newline = false;

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

    server_seen = false;
    adapter_seen = false;
    keyboard_seen = false;

    if (src != NULL) {
        while (fgets(line, sizeof(line), src) != NULL) {
            needs_newline = false;
            if (strncmp(line, "eamuse.server=", 14) == 0) {
                fprintf(dst, "eamuse.server=%s\n", page->server);
                server_seen = true;
            } else if (strncmp(line, "adapter.override_ip=", 20) == 0) {
                fprintf(dst, "adapter.override_ip=%s\n", page->adapter);
                adapter_seen = true;
            } else if (strncmp(line, "input.keyboard=", 15) == 0) {
                fprintf(dst, "input.keyboard=%s\n", page->keyboard ? "true" : "false");
                keyboard_seen = true;
            } else {
                size_t len = strlen(line);
                fputs(line, dst);
                needs_newline = len != 0 && line[len - 1] != '\n';
            }
        }
        fclose(src);
    }

    if (needs_newline) {
        fputc('\n', dst);
    }
    if (!server_seen) {
        fprintf(dst, "eamuse.server=%s\n", page->server);
    }
    if (!adapter_seen) {
        fprintf(dst, "adapter.override_ip=%s\n", page->adapter);
    }

    if (!keyboard_seen) {
        fprintf(dst, "input.keyboard=%s\n", page->keyboard ? "true" : "false");
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
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) page);
            SetDlgItemTextA(hwnd, IDC_GFDM_SERVER, page->server);
            SetDlgItemTextA(hwnd, IDC_GFDM_ADAPTER, page->adapter);
            CheckDlgButton(hwnd, IDC_GFDM_KEYBOARD,
                page->keyboard ? BST_CHECKED : BST_UNCHECKED);
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
