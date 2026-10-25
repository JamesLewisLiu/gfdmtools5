// clang-format off
// Don't format because the order is important here
#include <windows.h>
#include <commctrl.h>
#include <wchar.h>
// clang-format on

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/bind-adv.h"
#include "config/bind.h"
#include "config/action-order.h"
#include "config/resource.h"
#include "config/schema.h"
#include "config/usages.h"

#include "geninput/input-config.h"
#include "geninput/kbd-naive.h"
#include "geninput/mapper.h"

#include "util/defs.h"
#include "util/log.h"
#include "util/mem.h"
#include "util/winres.h"

static INT_PTR CALLBACK
buttons_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static bool buttons_get_control_name(
    const struct mapped_action *ma, wchar_t *str, size_t nchars);
static INT_PTR buttons_handle_init(HWND hwnd, const PROPSHEETPAGE *psp);
static INT_PTR buttons_handle_binding_adv(HWND hwnd);
static INT_PTR buttons_handle_binding_clear(HWND hwnd);
static INT_PTR
buttons_handle_binding_edit(HWND hwnd, BOOL bind_many, bool naive);
static INT_PTR buttons_handle_switch_page(HWND hwnd, NMUPDOWN *n);
static INT_PTR buttons_handle_fini(HWND hwnd);
static void buttons_update(HWND hwnd);
static void buttons_update_bindings(HWND hwnd);
static void buttons_update_pager(HWND hwnd);

struct buttons_tab {
    const struct schema *schema;
    uint8_t page_no;
    uint8_t npages;
};

HPROPSHEETPAGE
buttons_tab_create(HINSTANCE inst, const struct schema *schema)
{
    PROPSHEETPAGE psp;

    memset(&psp, 0, sizeof(psp));
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = inst;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_TAB_BUTTONS);
    psp.pfnDlgProc = buttons_dlg_proc;
    psp.lParam = (LPARAM) schema;

    return CreatePropertySheetPage(&psp);
}

static INT_PTR CALLBACK
buttons_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    NMHDR *n;

    switch (msg) {
        case WM_INITDIALOG:
            return buttons_handle_init(hwnd, (const PROPSHEETPAGE *) lparam);

        case WM_NOTIFY:
            n = (NMHDR *) lparam;

            switch (n->idFrom) {
                case IDC_PAGE:
                    switch (n->code) {
                        case UDN_DELTAPOS:
                            return buttons_handle_switch_page(
                                hwnd, (NMUPDOWN *) n);

                        default:
                            return FALSE;
                    }

                case IDC_BINDINGS:
                    switch (n->code) {
                        case NM_DBLCLK:
                            return buttons_handle_binding_edit(
                                hwnd, FALSE, false);

                        default:
                            return FALSE;
                    }

                default:
                    return FALSE;
            }

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_BINDING_ADV:
                    return buttons_handle_binding_adv(hwnd);

                case IDC_BINDING_CLEAR:
                    return buttons_handle_binding_clear(hwnd);

                case IDC_BINDING_EDIT:
                    return buttons_handle_binding_edit(hwnd, FALSE, false);

                case IDC_BINDING_EDIT_MANY:
                    return buttons_handle_binding_edit(hwnd, TRUE, false);

                case IDC_BINDING_NAIVE:
                    return buttons_handle_binding_edit(hwnd, FALSE, true);

                case IDC_BINDING_NAIVE_MANY:
                    return buttons_handle_binding_edit(hwnd, TRUE, true);

                default:
                    return FALSE;
            }

        case WM_DESTROY:
            return buttons_handle_fini(hwnd);

        default:
            return FALSE;
    }
}

static INT_PTR buttons_handle_init(HWND hwnd, const PROPSHEETPAGE *psp)
{
    struct buttons_tab *self;
    HWND bindings_ctl;
    HINSTANCE inst;
    LVCOLUMN col;
    LVITEM item;
    wchar_t str[128];
    size_t i;

    self = xmalloc(sizeof(*self));
    self->schema = (const struct schema *) psp->lParam;
    self->page_no = 0;

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) self);

    inst = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    bindings_ctl = GetDlgItem(hwnd, IDC_BINDINGS);

    ListView_SetExtendedListViewStyle(
        bindings_ctl, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LoadString(inst, IDS_COL_ACTION, str, lengthof(str));

    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    col.fmt = LVCFMT_LEFT;
    col.cx = 120;
    col.pszText = str;

    ListView_InsertColumn(bindings_ctl, 0, &col);

    LoadString(inst, IDS_COL_BUTTON, str, lengthof(str));

    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    col.fmt = LVCFMT_LEFT;
    col.cx = 120;
    col.pszText = str;

    ListView_InsertColumn(bindings_ctl, 1, &col);

    for (i = 0; i < self->schema->nactions; i++) {
        memset(&item, 0, sizeof(item));

        LoadString(
            inst, self->schema->actions[
                gfdm_action_for_display_row(self->schema, i)].name_rsrc,
            str, lengthof(str));

        item.mask = LVIF_TEXT;
        item.iItem = (int) i;
        item.iSubItem = 0;
        item.pszText = str;

        ListView_InsertItem(bindings_ctl, &item);
    }

    buttons_update(hwnd);

    return TRUE;
}

static void buttons_update(HWND hwnd)
{
    buttons_update_pager(hwnd);
    buttons_update_bindings(hwnd);

    /* Flickery, but seems to be the only way to avoid label garbling when
       the page label changes. Need to do better. */
    InvalidateRect(hwnd, NULL, TRUE);
}

static void buttons_update_pager(HWND hwnd)
{
    struct buttons_tab *self;
    HINSTANCE inst;
    wchar_t str[128];

    inst = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    self->npages = mapper_get_npages() + 1;

    rswprintf(
        str, lengthof(str), inst, IDS_PAGE, self->page_no + 1, self->npages);
    SetWindowText(GetDlgItem(hwnd, IDC_PAGE_TEXT), str);
}

static void buttons_update_bindings(HWND hwnd)
{
    action_iter_t pos;
    struct mapped_action ma;
    struct buttons_tab *self;
    HINSTANCE inst;
    HWND bindings_ctl;
    LVITEM item;
    wchar_t str[128];
    size_t i;

    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    inst = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    bindings_ctl = GetDlgItem(hwnd, IDC_BINDINGS);

    /* Clear binding cell text */

    for (i = 0; i < self->schema->nactions; i++) {
        memset(&item, 0, sizeof(item));

        item.mask = LVIF_TEXT;
        item.iItem = (int) i;
        item.iSubItem = 1;
        item.pszText = L"";

        ListView_SetItem(bindings_ctl, &item);
    }

    /* Repopulate bindings */

    hid_mgr_lock();

    for (pos = mapper_iterate_actions(); action_iter_is_valid(pos);
         action_iter_next(pos)) {
        if (action_iter_get_page(pos) != self->page_no) {
            continue;
        }

        action_iter_get_mapping(pos, &ma);

        if (!buttons_get_control_name(&ma, str, lengthof(str))) {
            /* Can't read device metadata, device is probably unplugged */
            LoadString(inst, IDS_NOT_PRESENT, str, lengthof(str));
        }

        memset(&item, 0, sizeof(item));

        item.mask = LVIF_TEXT;
        item.iItem = (int) gfdm_display_row_for_action(
            self->schema, action_iter_get_action(pos));
        item.iSubItem = 1;
        item.pszText = str;

        ListView_SetItem(bindings_ctl, &item);
    }

    action_iter_free(pos);
    hid_mgr_unlock();
}

static bool buttons_get_control_name(
    const struct mapped_action *ma, wchar_t *str, size_t nchars)
{
    char chars[128];
    struct hid_control *controls;
    size_t ncontrols;
    UINT scan;
    wchar_t key_name[96];

    if (!hid_stub_get_controls(ma->hid, NULL, &ncontrols)) {
        goto count_fail;
    }

    controls = xmalloc(ncontrols * sizeof(*controls));

    if (!hid_stub_get_controls(ma->hid, controls, &ncontrols)) {
        goto data_fail;
    }

    if (ma->control_no >= ncontrols) {
        goto bounds_fail;
    }

    usages_get(chars, lengthof(chars), controls[ma->control_no].usage);

    if (strcmp(hid_stub_get_dev_node(ma->hid), KBD_NAIVE_DEV_NODE) == 0) {
        scan = MapVirtualKeyW(ma->control_no, MAPVK_VK_TO_VSC_EX);

        if (scan != 0 &&
            GetKeyNameTextW(
                (scan & 0xFF) << 16 | (scan & 0xFF00 ? 1 << 24 : 0),
                key_name,
                lengthof(key_name)) != 0) {
            swprintf(str, nchars, L"%ls (Naive)", key_name);
        } else {
            swprintf(str, nchars, L"Naive VK 0x%02X", ma->control_no);
        }

        free(controls);

        return true;
    }

    MultiByteToWideChar(CP_UTF8, 0, chars, lengthof(chars), str, nchars);

    free(controls);

    return true;

bounds_fail:
data_fail:
    free(controls);

count_fail:
    return false;
}

static INT_PTR buttons_handle_binding_adv(HWND hwnd)
{
    HINSTANCE inst;
    struct mapped_action ma;
    struct buttons_tab *self;
    int action_no;
    uint8_t bit;
    bool is_valid;

    inst = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    action_no = ListView_GetSelectionMark(GetDlgItem(hwnd, IDC_BINDINGS));

    if (action_no < 0 || action_no >= (int) self->schema->nactions) {
        return TRUE;
    }

    action_no = gfdm_action_for_display_row(self->schema, action_no);
    bit = self->schema->actions[action_no].bit;

    is_valid = mapper_get_action_map((uint8_t) action_no, self->page_no, &ma);

    if (bind_adv(inst, hwnd, &ma, is_valid)) {
        mapper_set_action_map((uint8_t) action_no, self->page_no, bit, &ma);
        buttons_update(hwnd);
    }

    return TRUE;
}

static INT_PTR buttons_handle_binding_clear(HWND hwnd)
{
    struct buttons_tab *self;
    int action_no;

    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    action_no = ListView_GetSelectionMark(GetDlgItem(hwnd, IDC_BINDINGS));

    if (action_no < 0 || action_no >= (int) self->schema->nactions) {
        return TRUE;
    }

    action_no = gfdm_action_for_display_row(self->schema, action_no);
    mapper_clear_action_map((uint8_t) action_no, self->page_no);
    buttons_update(hwnd);

    return TRUE;
}

static INT_PTR
buttons_handle_binding_edit(HWND hwnd, BOOL bind_many, bool naive)
{
    HINSTANCE inst;
    HWND listview;
    struct buttons_tab *self;
    struct mapped_action ma;
    int action_start;
    int action_no;
    int row;
    uint8_t bit;

    inst = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    listview = GetDlgItem(hwnd, IDC_BINDINGS);

    action_start = ListView_GetSelectionMark(listview);

    if (action_start < 0 || action_start >= (int) self->schema->nactions) {
        /* wtf... */
        return TRUE;
    }

    for (row = action_start; row < self->schema->nactions; row++) {
        // Make sure the next listview item is visible
        ListView_SetItemState(
            listview, row, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
        ListView_EnsureVisible(listview, row, FALSE);

        action_no = gfdm_action_for_display_row(self->schema, row);
        bit = self->schema->actions[action_no].bit;

        if (bind_control(
                inst, hwnd, &self->schema->actions[action_no], &ma, naive)) {
            mapper_set_action_map((uint8_t) action_no, self->page_no, bit, &ma);
            buttons_update(hwnd);

            log_misc(
                "Bind act %d -> dev %p ctl %u range [%d, %d]",
                self->schema->actions[action_no].bit,
                ma.hid,
                (unsigned int) ma.control_no,
                ma.value_min,
                ma.value_max);
        } else {
            break;
        }

        if (!bind_many) {
            break;
        }
    }

    return TRUE;
}

static INT_PTR buttons_handle_switch_page(HWND hwnd, NMUPDOWN *n)
{
    struct buttons_tab *self;

    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    /* The buttons on a horizontal spinner are backwards -.- */

    if (n->iDelta > 0 && self->page_no > 0) {
        self->page_no--;
        buttons_update(hwnd);
    } else if (n->iDelta < 0 && self->page_no < self->npages - 1) {
        self->page_no++;
        buttons_update(hwnd);
    }

    return TRUE;
}

static INT_PTR buttons_handle_fini(HWND hwnd)
{
    struct buttons_tab *self;

    self = (struct buttons_tab *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    free(self);

    return TRUE;
}
