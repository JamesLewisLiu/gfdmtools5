#include <windows.h>

#include <stdbool.h>

#include "gfdmhook1/display.h"
#include "util/log.h"

void gfdm_display_init(HWND window, bool fullscreen)
{
    int(__cdecl * set_fullscreen)(void);
    int(__cdecl * set_windowscreen)(void);
    HMODULE system;
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    RECT client, frame;
    LONG_PTR style, ex_style;
    int width, height, x = 0, y = 0;
    bool active = false;

    if (window == NULL || !GetClientRect(window, &client)) {
        log_warning("GFDM display: main window is unavailable");
        return;
    }
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    system = GetModuleHandleA("libsystem.dll");
    set_fullscreen = system != NULL ?
        (void *) GetProcAddress(system, "sys_gl_set_fullscreen") :
        NULL;
    set_windowscreen = system != NULL ?
        (void *) GetProcAddress(system, "sys_gl_set_windowscreen") :
        NULL;

    /* Both exports are cdecl int(void) in V4-V8. Fullscreen toggles, so
       establish window mode before the one startup request. Native finalize
       restores the desktop using the state owned by libsystem itself. */
    if (set_windowscreen != NULL) {
        set_windowscreen();
        if (fullscreen && set_fullscreen != NULL) {
            active = set_fullscreen() != 0;
        }
    }
    if (fullscreen && !active) {
        log_warning(
            "GFDM display: native fullscreen unavailable; using a window");
    }

    style = GetWindowLongPtr(window, GWL_STYLE);
    ex_style = GetWindowLongPtr(window, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
    style &= ~(WS_POPUP | WS_OVERLAPPEDWINDOW);
    style |= active ? WS_POPUP : WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    SetWindowLongPtr(window, GWL_STYLE, style);
    SetWindowLongPtr(window, GWL_EXSTYLE, ex_style);
    frame = (RECT) {0, 0, width, height};
    if (!active) {
        AdjustWindowRectEx(&frame, (DWORD) style, FALSE, (DWORD) ex_style);
        if (GetMonitorInfo(
                MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY),
                &monitor)) {
            x = monitor.rcWork.left +
                (monitor.rcWork.right - monitor.rcWork.left -
                 (frame.right - frame.left)) /
                    2;
            y = monitor.rcWork.top +
                (monitor.rcWork.bottom - monitor.rcWork.top -
                 (frame.bottom - frame.top)) /
                    2;
            if (x < monitor.rcWork.left)
                x = monitor.rcWork.left;
            if (y < monitor.rcWork.top)
                y = monitor.rcWork.top;
        }
        SetClassLongPtr(
            window, GCLP_HCURSOR, (LONG_PTR) LoadCursor(NULL, IDC_ARROW));
        /* Native startup hides the cursor for both its temporary GL window
           and the game window. Balance those calls for windowed operation. */
        while (ShowCursor(TRUE) < 0) {
        }
    }
    SetWindowPos(
        window,
        HWND_NOTOPMOST,
        x,
        y,
        frame.right - frame.left,
        frame.bottom - frame.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    log_info(
        "GFDM display: %s, client %dx%d (restart to change)",
        active ? "fullscreen" : "windowed",
        width,
        height);
}
