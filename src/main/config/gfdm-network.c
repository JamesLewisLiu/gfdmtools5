#include <windows.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "config/resource.h"
#include "config/schema.h"

#include "util/defs.h"
#include "util/log.h"

struct gfdm_network_page {
    HWND hwnd;
    char mode[3];
    char server[256];
    char adapter[64];
};

static INT_PTR CALLBACK gfdm_network_dlg_proc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void gfdm_network_filename(
    const struct gfdm_network_page *page, char *path, size_t path_len)
{
    snprintf(path, path_len, "gfdm-v4-%s.conf", page->mode);
}

static void gfdm_network_load(struct gfdm_network_page *page)
{
    char path[MAX_PATH];
    char line[512];
    FILE *f;

    strcpy(page->server, "localhost:80");
    page->adapter[0] = '\0';
    gfdm_network_filename(page, path, sizeof(path));
    f = fopen(path, "rb");

    if (f == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "eamuse.server=", 14) == 0) {
            sscanf(line + 14, "%255[^\r\n]", page->server);
        } else if (strncmp(line, "adapter.override_ip=", 21) == 0) {
            sscanf(line + 21, "%63[^\r\n]", page->adapter);
        }
    }

    fclose(f);
}

static void gfdm_network_save(struct gfdm_network_page *page)
{
    char path[MAX_PATH];
    char tmp_path[MAX_PATH];
    char line[512];
    FILE *src;
    FILE *dst;
    bool server_seen;
    bool adapter_seen;

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

    gfdm_network_filename(page, path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    src = fopen(path, "rb");
    dst = fopen(tmp_path, "wb");

    if (dst == NULL) {
        log_warning("Could not write %s", path);
        if (src != NULL) {
            fclose(src);
        }
        return;
    }

    server_seen = false;
    adapter_seen = false;

    if (src != NULL) {
        while (fgets(line, sizeof(line), src) != NULL) {
            if (strncmp(line, "eamuse.server=", 14) == 0) {
                fprintf(dst, "eamuse.server=%s\n", page->server);
                server_seen = true;
            } else if (strncmp(line, "adapter.override_ip=", 21) == 0) {
                fprintf(dst, "adapter.override_ip=%s\n", page->adapter);
                adapter_seen = true;
            } else {
                fputs(line, dst);
            }
        }
        fclose(src);
    }

    if (!server_seen) {
        fprintf(dst, "eamuse.server=%s\n", page->server);
    }
    if (!adapter_seen) {
        fprintf(dst, "adapter.override_ip=%s\n", page->adapter);
    }

    fclose(dst);
    DeleteFileA(path);
    MoveFileA(tmp_path, path);
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
            gfdm_network_load(page);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) page);
            SetDlgItemTextA(hwnd, IDC_GFDM_SERVER, page->server);
            SetDlgItemTextA(hwnd, IDC_GFDM_ADAPTER, page->adapter);
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
