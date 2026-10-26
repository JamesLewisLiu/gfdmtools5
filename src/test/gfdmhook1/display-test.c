#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static HMODULE test_module(const char *name);
static void *test_export(HMODULE module, const char *name);
#define GetModuleHandleA test_module
#define GetProcAddress test_export
#include "gfdmhook1/display.c"
#undef GetModuleHandleA
#undef GetProcAddress

static int window_calls, full_calls, native_fullscreen, full_success;
static bool exports_ready;

static int __cdecl windowscreen(void)
{
    window_calls++;
    native_fullscreen = 0;
    return 1;
}

static int __cdecl fullscreen(void)
{
    full_calls++;
    assert(native_fullscreen == 0); /* Calling twice would toggle it off. */
    native_fullscreen = full_success;
    return full_success;
}

static HMODULE test_module(const char *name)
{
    assert(!strcmp(name, "libsystem.dll"));
    return (HMODULE) 1;
}

static void *test_export(HMODULE module, const char *name)
{
    assert(module == (HMODULE) 1);
    if (!exports_ready)
        return NULL;
    if (!strcmp(name, "sys_gl_set_fullscreen"))
        return (void *) fullscreen;
    assert(!strcmp(name, "sys_gl_set_windowscreen"));
    return (void *) windowscreen;
}

static void check_window(bool requested, bool available, int success)
{
    RECT rect;
    const DWORD style = WS_POPUP;
    HWND window = CreateWindowExA(
        0,
        "GFDMDisplayTest",
        "GFDM display test",
        style,
        0,
        0,
        640,
        480,
        NULL,
        NULL,
        GetModuleHandleA(NULL),
        NULL);
    assert(window != NULL);
    window_calls = full_calls = native_fullscreen = 0;
    full_success = success;
    exports_ready = available;
    gfdm_display_init(window, requested);
    bool active = requested && available && success;
    assert(native_fullscreen == active);
    assert(window_calls == available);
    assert(full_calls == (requested && available));
    assert(GetClientRect(window, &rect));
    assert(
        rect.right == 640 && rect.bottom == 480); /* No GL viewport stretch. */
    LONG_PTR actual_style = GetWindowLongPtr(window, GWL_STYLE);
    assert(!(GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_TOPMOST));
    if (active) {
        assert(actual_style & WS_POPUP);
        assert(!(actual_style & WS_CAPTION));
    } else {
        assert(!(actual_style & WS_POPUP));
        assert((actual_style & WS_CAPTION) == WS_CAPTION);
        assert(actual_style & WS_SYSMENU);
        assert(!(actual_style & (WS_THICKFRAME | WS_MAXIMIZEBOX)));
    }
    assert(DestroyWindow(window));
}

int main(void)
{
    WNDCLASSA cls = {0};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "GFDMDisplayTest";
    assert(RegisterClassA(&cls));
    check_window(false, true, 1);
    check_window(true, true, 1);
    check_window(true, true, 0);
    check_window(true, false, 1);
    gfdm_display_init(NULL, true);
    assert(UnregisterClassA(cls.lpszClassName, cls.hInstance));
    puts(
        "PASS: native fullscreen startup, desktop-reset ordering, fixed client "
        "size, framed window, unsupported-mode/export fallback and unavailable "
        "window");
    return 0;
}
