#include <GL/gl.h>
#include <windows.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gfdmhook1/display.h"
#include "hook/table.h"
#include "util/log.h"

static struct {
    HWND window;
    WNDPROC wndproc;
    bool unicode, placing, requesting, framebuffer;
    HMONITOR monitor;
    HGLRC context;
    int mode, width, height, refresh;
    RECT output;
    GLint viewport[4], scissor[4];
    DEVMODEA desktop_a;
    DEVMODEW desktop_w;
} display;

static void(APIENTRY *real_viewport)(GLint, GLint, GLsizei, GLsizei);
static void(APIENTRY *real_scissor)(GLint, GLint, GLsizei, GLsizei);
static LONG(WINAPI *real_change_a)(DEVMODEA *, DWORD);
static LONG(WINAPI *real_change_w)(DEVMODEW *, DWORD);
static HWND(__cdecl *real_window_init)(HINSTANCE, WNDPROC);
static BOOL(WINAPI *real_make_current)(HDC, HGLRC);
static BOOL(WINAPI *real_delete_context)(HGLRC);
static void display_context(HDC dc);

static bool display_scaled(void)
{
    return display.window != NULL && display.mode != GFDM_DISPLAY_WINDOWED;
}

static bool display_stretched(void)
{
    return display.mode == GFDM_DISPLAY_FULLSCREEN_STRETCH ||
        display.mode == GFDM_DISPLAY_BORDERLESS_STRETCH;
}

static void display_output(void)
{
    RECT client;
    int width, height, w, h;
    if (!GetClientRect(display.window, &client))
        return;
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
        return;
    w = width;
    h = height;
    if (!display_stretched()) {
        if ((int64_t) width * display.height > (int64_t) height * display.width)
            w = (int) ((int64_t) height * display.width / display.height);
        else
            h = (int) ((int64_t) width * display.height / display.width);
    }
    display.output.left = (width - w) / 2;
    display.output.bottom = (height - h) / 2;
    display.output.right = display.output.left + w;
    display.output.top = display.output.bottom + h;
}

static GLint display_coordinate(int64_t value, int size, int base, int offset)
{
    int64_t result = value >= 0 ? (value * size + base / 2) / base :
                                  -((-value * size + base / 2) / base);
    result += offset;
    return result < INT_MIN ? INT_MIN :
        result > INT_MAX    ? INT_MAX :
                              (GLint) result;
}

static void display_map(const GLint *logical, GLint *mapped)
{
    int width = display.output.right - display.output.left;
    int height = display.output.top - display.output.bottom;
    GLint right, top;
    mapped[0] = display_coordinate(
        logical[0], width, display.width, display.output.left);
    mapped[1] = display_coordinate(
        logical[1], height, display.height, display.output.bottom);
    right = display_coordinate(
        (int64_t) logical[0] + logical[2],
        width,
        display.width,
        display.output.left);
    top = display_coordinate(
        (int64_t) logical[1] + logical[3],
        height,
        display.height,
        display.output.bottom);
    mapped[2] = (GLint) ((int64_t) right - mapped[0] > INT_MAX ?
                             INT_MAX :
                             (int64_t) right - mapped[0]);
    mapped[3] = (GLint) ((int64_t) top - mapped[1] > INT_MAX ?
                             INT_MAX :
                             (int64_t) top - mapped[1]);
}

static bool display_current(void)
{
    GLint framebuffer = 0;
    if (!display_scaled() || display.context == NULL ||
        wglGetCurrentContext() != display.context ||
        WindowFromDC(wglGetCurrentDC()) != display.window)
        return false;
    if (display.framebuffer)
        glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING[_EXT] */, &framebuffer);
    return framebuffer == 0;
}

static void display_apply_rectangles(void)
{
    GLint rect[4];
    if (!display_current())
        return;
    display_output();
    display_map(display.viewport, rect);
    real_viewport(rect[0], rect[1], rect[2], rect[3]);
    display_map(display.scissor, rect);
    real_scissor(rect[0], rect[1], rect[2], rect[3]);
}

static void APIENTRY
display_viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLint rect[4] = {x, y, width, height};
    /* Preserve invalid calls and offscreen/helper-context behavior. */
    if (width >= 0 && height >= 0 && display_current()) {
        memcpy(display.viewport, rect, sizeof(rect));
        display_output();
        display_map(display.viewport, rect);
    }
    real_viewport(rect[0], rect[1], rect[2], rect[3]);
}

static void APIENTRY
display_scissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLint rect[4] = {x, y, width, height};
    if (width >= 0 && height >= 0 && display_current()) {
        memcpy(display.scissor, rect, sizeof(rect));
        display_output();
        display_map(display.scissor, rect);
    }
    real_scissor(rect[0], rect[1], rect[2], rect[3]);
}

static const struct hook_symbol display_gl_syms[] = {
    {.name = "glViewport", .patch = display_viewport},
    {.name = "glScissor", .patch = display_scissor},
};

static BOOL WINAPI display_make_current(HDC dc, HGLRC context)
{
    BOOL result = real_make_current(dc, context);
    if (result && display_scaled())
        display_context(dc);
    return result;
}

static BOOL WINAPI display_delete_context(HGLRC context)
{
    BOOL result = real_delete_context(context);
    if (result && context == display.context)
        display.context = NULL;
    return result;
}

void gfdm_display_apply_hooks(void)
{
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl == NULL)
        return;
    /* Use export pointers even if this DLL's own IAT is included in a scan. */
    real_viewport = (void *) GetProcAddress(gl, "glViewport");
    real_scissor = (void *) GetProcAddress(gl, "glScissor");
    real_make_current = (void *) GetProcAddress(gl, "wglMakeCurrent");
    real_delete_context = (void *) GetProcAddress(gl, "wglDeleteContext");
    if (real_viewport != NULL && real_scissor != NULL) {
        const struct hook_symbol context = {
            .name = "wglMakeCurrent", .patch = display_make_current};
        hook_table_apply(
            NULL,
            "opengl32.dll",
            display_gl_syms,
            sizeof(display_gl_syms) / sizeof(display_gl_syms[0]));
        if (real_make_current != NULL)
            hook_table_apply(NULL, "opengl32.dll", &context, 1);
        if (real_delete_context != NULL) {
            const struct hook_symbol deletion = {
                .name = "wglDeleteContext", .patch = display_delete_context};
            hook_table_apply(NULL, "opengl32.dll", &deletion, 1);
        }
    }
}

static void display_context(HDC dc)
{
    HGLRC context = wglGetCurrentContext();
    PROC binding;
    if (context == NULL || dc != wglGetCurrentDC() ||
        WindowFromDC(dc) != display.window || context == display.context)
        return;
    display.context = context;
    binding = wglGetProcAddress("glBindFramebuffer");
    if ((uintptr_t) binding <= 3 || (intptr_t) binding == -1)
        binding = wglGetProcAddress("glBindFramebufferEXT");
    display.framebuffer = (uintptr_t) binding > 3 && (intptr_t) binding != -1;
    /* libd3 caches these rectangles and may not reissue them after a context
       change. Reapply the last logical state, including partial scissors. */
    display_apply_rectangles();
}

void gfdm_display_present(HDC dc)
{
    RECT client;
    int width, height;
    if (!display_scaled())
        return;
    display_context(dc);
    if (!display_current() || dc != wglGetCurrentDC() ||
        !GetClientRect(display.window, &client))
        return;
    display_apply_rectangles();
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || display_stretched())
        return;
    /* Clear only the bars. Restore real GL state so libd3's cached state
       remains correct, including its draw buffer, color mask and scissor. */
    glPushAttrib(GL_SCISSOR_BIT | GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    real_scissor(0, 0, display.output.left, height);
    glClear(GL_COLOR_BUFFER_BIT);
    real_scissor(display.output.right, 0, width - display.output.right, height);
    glClear(GL_COLOR_BUFFER_BIT);
    real_scissor(0, 0, width, display.output.bottom);
    glClear(GL_COLOR_BUFFER_BIT);
    real_scissor(0, display.output.top, width, height - display.output.top);
    glClear(GL_COLOR_BUFFER_BIT);
    glPopAttrib();
}

static LONG WINAPI display_change_a(DEVMODEA *mode, DWORD flags)
{
    if (display.requesting && mode != NULL && (flags & CDS_FULLSCREEN)) {
        DEVMODEA request = display.desktop_a;
        if (display.refresh > 0) {
            request.dmFields |= DM_DISPLAYFREQUENCY;
            request.dmDisplayFrequency = display.refresh;
        }
        return real_change_a(&request, flags);
    }
    return real_change_a(mode, flags);
}

static LONG WINAPI display_change_w(DEVMODEW *mode, DWORD flags)
{
    if (display.requesting && mode != NULL && (flags & CDS_FULLSCREEN)) {
        DEVMODEW request = display.desktop_w;
        if (display.refresh > 0) {
            request.dmFields |= DM_DISPLAYFREQUENCY;
            request.dmDisplayFrequency = display.refresh;
        }
        return real_change_w(&request, flags);
    }
    return real_change_w(mode, flags);
}

static const struct hook_symbol display_mode_syms[] = {
    {.name = "ChangeDisplaySettingsA",
     .patch = display_change_a,
     .link = (void **) &real_change_a},
    {.name = "ChangeDisplaySettingsW",
     .patch = display_change_w,
     .link = (void **) &real_change_w},
};

static void display_place(void)
{
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    if (display.placing || !display_scaled() || IsIconic(display.window))
        return;
    if (!GetMonitorInfo(display.monitor, &monitor)) {
        display.monitor =
            MonitorFromWindow(display.window, MONITOR_DEFAULTTOPRIMARY);
        if (!GetMonitorInfo(display.monitor, &monitor))
            return;
    }
    display.placing = true;
    SetWindowPos(
        display.window,
        HWND_NOTOPMOST,
        monitor.rcMonitor.left,
        monitor.rcMonitor.top,
        monitor.rcMonitor.right - monitor.rcMonitor.left,
        monitor.rcMonitor.bottom - monitor.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
    display.placing = false;
}

static LRESULT CALLBACK
display_wndproc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC previous = display.wndproc;
    bool unicode = display.unicode;
    if (msg == WM_DPICHANGED && lp != 0 &&
        display.mode >= GFDM_DISPLAY_BORDERLESS_FIT) {
        display.monitor =
            MonitorFromRect((const RECT *) lp, MONITOR_DEFAULTTONEAREST);
    }
    if (msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED)
        display_place();
    if (msg == WM_SIZE || msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED)
        display_apply_rectangles();
    if (msg == WM_NCDESTROY) {
        display.window = NULL;
        display.context = NULL;
    }
    return unicode ? CallWindowProcW(previous, window, msg, wp, lp) :
                     CallWindowProcA(previous, window, msg, wp, lp);
}

static HWND __cdecl display_window_init(HINSTANCE instance, WNDPROC wndproc)
{
    BOOL(WINAPI * set_awareness)(HANDLE);
    set_awareness = (void *) GetProcAddress(
        GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    /* Runs before the native temporary GL and main HWND are created.
       Respect an existing manifest/host DPI policy if the call is refused. */
    if (set_awareness != NULL)
        set_awareness((HANDLE) (intptr_t) -4 /* per-monitor aware V2 */);
    else
        SetProcessDPIAware();
    return real_window_init(instance, wndproc);
}

void gfdm_display_prepare(void)
{
    const struct hook_symbol symbol = {
        .name = "sys_window_initialize",
        .patch = display_window_init,
        .link = (void **) &real_window_init};
    hook_table_apply(GetModuleHandleA(NULL), "libsystem.dll", &symbol, 1);
}

void gfdm_display_init(HWND window, int mode, int refresh)
{
    int(__cdecl * set_fullscreen)(void);
    int(__cdecl * set_windowscreen)(void);
    HMODULE system;
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    RECT client, frame;
    LONG_PTR style, ex_style;
    bool native = mode == GFDM_DISPLAY_FULLSCREEN_FIT ||
        mode == GFDM_DISPLAY_FULLSCREEN_STRETCH;
    int x = 0, y = 0;

    if (window == NULL || !GetClientRect(window, &client) ||
        client.right <= client.left || client.bottom <= client.top) {
        log_warning("GFDM display: main window is unavailable");
        return;
    }
    if (mode < GFDM_DISPLAY_WINDOWED ||
        mode > GFDM_DISPLAY_BORDERLESS_STRETCH) {
        mode = GFDM_DISPLAY_WINDOWED;
        native = false;
    }
    memset(&display, 0, sizeof(display));
    display.window = window;
    display.mode = mode;
    display.width = client.right - client.left;
    display.height = client.bottom - client.top;
    display.viewport[2] = display.width;
    display.viewport[3] = display.height;
    memcpy(display.scissor, display.viewport, sizeof(display.scissor));
    display.refresh = refresh;
    display.monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    system = GetModuleHandleA("libsystem.dll");
    set_fullscreen = system != NULL ?
        (void *) GetProcAddress(system, "sys_gl_set_fullscreen") :
        NULL;
    set_windowscreen = system != NULL ?
        (void *) GetProcAddress(system, "sys_gl_set_windowscreen") :
        NULL;
    if (set_windowscreen != NULL)
        set_windowscreen();
    if (native) {
        display.desktop_a.dmSize = sizeof(display.desktop_a);
        display.desktop_w.dmSize = sizeof(display.desktop_w);
        if (set_fullscreen != NULL && set_windowscreen != NULL &&
            EnumDisplaySettingsA(
                NULL, ENUM_CURRENT_SETTINGS, &display.desktop_a) &&
            EnumDisplaySettingsW(
                NULL, ENUM_CURRENT_SETTINGS, &display.desktop_w)) {
            /* The cdecl fullscreen export toggles and owns desktop restoration.
               Override only its startup output request; keep the original
               logical dimensions and projection intact. */
            hook_table_apply(
                system,
                "user32.dll",
                display_mode_syms,
                sizeof(display_mode_syms) / sizeof(display_mode_syms[0]));
            display.requesting = true;
            if (!set_fullscreen())
                display.mode = GFDM_DISPLAY_WINDOWED;
            display.requesting = false;
            display.monitor =
                MonitorFromPoint((POINT) {0, 0}, MONITOR_DEFAULTTOPRIMARY);
        } else {
            display.mode = GFDM_DISPLAY_WINDOWED;
        }
        if (display.mode == GFDM_DISPLAY_WINDOWED)
            log_warning(
                "GFDM display: native fullscreen unavailable; using a window");
    }
    gfdm_display_apply_hooks();
    if (display_scaled() && (real_viewport == NULL || real_scissor == NULL)) {
        if (native && set_windowscreen != NULL)
            set_windowscreen();
        display.mode = GFDM_DISPLAY_WINDOWED;
        log_warning("GFDM display: OpenGL mapping unavailable; using a window");
    }
    style =
        GetWindowLongPtr(window, GWL_STYLE) & ~(WS_POPUP | WS_OVERLAPPEDWINDOW);
    ex_style = GetWindowLongPtr(window, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
    style |=
        display_scaled() ? WS_POPUP : WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    SetWindowLongPtr(window, GWL_STYLE, style);
    SetWindowLongPtr(window, GWL_EXSTYLE, ex_style);
    frame = (RECT) {0, 0, display.width, display.height};
    if (display_scaled() && GetMonitorInfo(display.monitor, &monitor)) {
        frame = (RECT) {
            0,
            0,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top};
        x = monitor.rcMonitor.left;
        y = monitor.rcMonitor.top;
    } else if (!display_scaled()) {
        AdjustWindowRectEx(&frame, (DWORD) style, FALSE, (DWORD) ex_style);
        if (GetMonitorInfo(display.monitor, &monitor)) {
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
    display.unicode = IsWindowUnicode(window);
    SetLastError(0);
    display.wndproc = (WNDPROC) SetWindowLongPtr(
        window, GWLP_WNDPROC, (LONG_PTR) display_wndproc);
    if (display.wndproc == NULL)
        log_warning(
            "GFDM display: cannot monitor window lifecycle (%lu)",
            GetLastError());
    display_context(wglGetCurrentDC());
    log_info(
        "GFDM display: mode %d, logical %dx%d, output %ldx%ld (restart to "
        "change)",
        display.mode,
        display.width,
        display.height,
        frame.right - frame.left,
        frame.bottom - frame.top);
}
