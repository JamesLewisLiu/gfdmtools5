#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include <string.h>

#include "gfdmhook1/display.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

static HWND display_window;
static WNDPROC display_original_proc;
static bool display_fullscreen;
static LONG_PTR display_original_style, display_original_exstyle;
static RECT display_original_rect;
static WINDOWPLACEMENT display_original_placement;
static void (APIENTRY *display_real_viewport)(GLint, GLint, GLsizei, GLsizei);
static void (APIENTRY *display_real_scissor)(GLint, GLint, GLsizei, GLsizei);

/* Rendering stays in the game's 640x480 logical coordinates. */
static RECT display_fit(int width, int height)
{
    RECT r = {0, 0, width, height};
    if ((int64_t) width * 3 > (int64_t) height * 4) {
        int fit = (int) ((int64_t) height * 4 / 3);
        r.left = (width - fit) / 2; r.right = r.left + fit;
    } else {
        int fit = (int) ((int64_t) width * 3 / 4);
        r.top = (height - fit) / 2; r.bottom = r.top + fit;
    }
    return r;
}

static bool display_target(void)
{
    HDC dc = wglGetCurrentDC();
    return display_fullscreen && display_window != NULL && dc != NULL &&
        WindowFromDC(dc) == display_window;
}

static bool display_client(RECT *client, RECT *fit)
{
    if (!GetClientRect(display_window, client) || client->right <= 0 ||
        client->bottom <= 0 || IsIconic(display_window)) return false;
    *fit = display_fit(client->right, client->bottom);
    return true;
}

static void display_bounds(void)
{
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    HMONITOR handle = MonitorFromWindow(display_window, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(handle, &monitor)) {
        SetWindowPos(display_window, HWND_TOP,
            monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

static void display_set_fullscreen(bool enabled)
{
    if (display_window == NULL || enabled == display_fullscreen) return;
    if (enabled) {
        display_original_style = GetWindowLongPtrW(display_window, GWL_STYLE);
        display_original_exstyle = GetWindowLongPtrW(display_window, GWL_EXSTYLE);
        GetWindowRect(display_window, &display_original_rect);
        memset(&display_original_placement, 0, sizeof(display_original_placement));
        display_original_placement.length = sizeof(display_original_placement);
        GetWindowPlacement(display_window, &display_original_placement);
        display_fullscreen = true;
        SetWindowLongPtrW(display_window, GWL_STYLE,
            (display_original_style & ~(WS_OVERLAPPEDWINDOW | WS_CHILD)) | WS_POPUP);
        SetWindowLongPtrW(display_window, GWL_EXSTYLE,
            display_original_exstyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
        display_bounds();
    } else {
        display_fullscreen = false;
        SetWindowLongPtrW(display_window, GWL_STYLE, display_original_style);
        SetWindowLongPtrW(display_window, GWL_EXSTYLE, display_original_exstyle);
        SetWindowPlacement(display_window, &display_original_placement);
        SetWindowPos(display_window,
            (display_original_exstyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST,
            display_original_rect.left, display_original_rect.top,
            display_original_rect.right - display_original_rect.left,
            display_original_rect.bottom - display_original_rect.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
    log_info("GFDM display: %s (Alt+Enter toggles)",
        enabled ? "fullscreen, aspect 4:3" : "original window restored");
}

static LRESULT CALLBACK display_wndproc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1L << 29))) {
        if (!(lp & (1L << 30))) display_set_fullscreen(!display_fullscreen);
        return 0;
    }
    if ((msg == WM_SYSKEYUP || msg == WM_SYSCHAR) && wp == VK_RETURN) return 0;
    if (msg == WM_SETCURSOR && display_fullscreen && LOWORD(lp) == HTCLIENT) {
        SetCursor(NULL); return TRUE;
    }
    WNDPROC original = display_original_proc;
    LRESULT result = CallWindowProcW(original, window, msg, wp, lp);
    if ((msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED) && display_fullscreen)
        display_bounds();
    if (msg == WM_NCDESTROY) {
        display_window = NULL; display_original_proc = NULL;
    }
    return result;
}

static void display_adopt(HDC dc)
{
    wchar_t title[32];
    HWND window = WindowFromDC(dc);
    if (display_window != NULL || window == NULL ||
        GetAncestor(window, GA_ROOT) != window ||
        GetWindowTextW(window, title, lengthof(title)) == 0 ||
        wcscmp(title, L"GFDM") != 0) return;
    SetLastError(0);
    WNDPROC original = (WNDPROC) SetWindowLongPtrW(window, GWLP_WNDPROC,
        (LONG_PTR) display_wndproc);
    if (original == NULL) {
        log_warning("GFDM display: could not subclass main window (%lu)", GetLastError());
        return;
    }
    display_window = window; display_original_proc = original;
    bool requested = display_fullscreen;
    display_fullscreen = false;
    if (requested) display_set_fullscreen(true);
}

static void display_transform(GLint *x, GLint *y, GLsizei *width, GLsizei *height)
{
    RECT client, fit;
    if (!display_target() || *width < 0 || *height < 0 ||
        !display_client(&client, &fit)) return;
    int w = fit.right - fit.left, h = fit.bottom - fit.top;
    GLint right = (GLint) (((int64_t) *x + *width) * w / 640);
    GLint top = (GLint) (((int64_t) *y + *height) * h / 480);
    *x = (GLint) ((int64_t) *x * w / 640);
    *y = (GLint) ((int64_t) *y * h / 480);
    *width = right - *x; *height = top - *y;
    *x += fit.left; *y += fit.top;
}

static void APIENTRY display_viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    display_transform(&x, &y, &width, &height);
    display_real_viewport(x, y, width, height);
}

static void APIENTRY display_scissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    display_transform(&x, &y, &width, &height);
    display_real_scissor(x, y, width, height);
}

static const struct hook_symbol display_gl_syms[] = {
    {.name="glViewport", .patch=display_viewport, .link=(void **) &display_real_viewport},
    {.name="glScissor", .patch=display_scissor, .link=(void **) &display_real_scissor},
};

void gfdm_display_init(bool fullscreen)
{
    display_fullscreen = fullscreen;
}

void gfdm_display_apply_hooks(void)
{
    hook_table_apply(NULL, "opengl32.dll", display_gl_syms, lengthof(display_gl_syms));
}

static void display_clear_rect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    display_real_scissor(x, y, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gfdm_display_present(HDC dc)
{
    RECT client, fit;
    display_adopt(dc);
    if (!display_real_scissor || !display_target() ||
        !display_client(&client, &fit)) return;
    /* Clear only letterbox/pillarbox regions. Preserve the game's GL state. */
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_SCISSOR_BIT | GL_ENABLE_BIT);
    glDrawBuffer(GL_BACK);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    display_clear_rect(0, 0, fit.left, client.bottom);
    display_clear_rect(fit.right, 0, client.right - fit.right, client.bottom);
    display_clear_rect(fit.left, 0, fit.right - fit.left, fit.top);
    display_clear_rect(fit.left, fit.bottom, fit.right - fit.left, client.bottom - fit.bottom);
    glPopAttrib();
}
