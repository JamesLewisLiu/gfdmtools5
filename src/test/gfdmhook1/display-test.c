#undef NDEBUG
#define LOG_SUPPRESS
#include <assert.h>
#include <stdio.h>
#include "gfdmhook1/display.c"

/* Exercise the production window/GL behavior without patching the test IAT. */
void hook_table_apply(HMODULE target, const char *dependency,
    const struct hook_symbol *symbols, size_t count)
{
    (void) target; (void) dependency; (void) symbols; (void) count;
}

static void equal_rect(RECT a, RECT b)
{
    assert(EqualRect(&a, &b));
}

static void pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char p[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    assert(p[0] == r && p[1] == g && p[2] == b);
}

int main(void)
{
    const RECT wide = {240, 0, 1680, 1080};
    const RECT tall = {0, 120, 480, 480};
    equal_rect(display_fit(1920, 1080), wide);
    equal_rect(display_fit(480, 600), tall);
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"GFDM-display-test";
    cls.style = CS_OWNDC;
    assert(RegisterClassW(&cls));
    HWND window = CreateWindowW(cls.lpszClassName, L"GFDM", WS_POPUP,
        40, 50, 640, 480, NULL, NULL, cls.hInstance, NULL);
    assert(window);
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format = {0};
    format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 24;
    int index = ChoosePixelFormat(dc, &format);
    assert(index && SetPixelFormat(dc, index, &format));
    HGLRC context = wglCreateContext(dc);
    assert(context && wglMakeCurrent(dc, context));
    ShowWindow(window, SW_SHOWNOACTIVATE);
    display_real_viewport = glViewport;
    display_real_scissor = glScissor;
    RECT original, client, fit, bounds;
    GetWindowRect(window, &original);
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    gfdm_display_init(false);
    gfdm_display_present(dc);
    assert(display_window == window && !display_fullscreen);
    display_viewport(0, 0, 640, 480);
    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
    assert(viewport[0] == 0 && viewport[1] == 0 && viewport[2] == 640 && viewport[3] == 480);
    SendMessageW(window, WM_SYSKEYDOWN, VK_RETURN, (LPARAM) 1 << 29);
    assert(display_fullscreen && display_client(&client, &fit));
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    assert(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor));
    GetWindowRect(window, &bounds); equal_rect(bounds, monitor.rcMonitor);
    SendMessageW(window, WM_SYSKEYDOWN, VK_RETURN, ((LPARAM) 1 << 29) | ((LPARAM) 1 << 30));
    assert(display_fullscreen); /* auto-repeat cannot flip again */
    display_viewport(0, 0, 640, 480);
    glGetIntegerv(GL_VIEWPORT, viewport);
    assert(viewport[0] == fit.left && viewport[1] == fit.top &&
        viewport[2] == fit.right - fit.left && viewport[3] == fit.bottom - fit.top);
    glEnable(GL_SCISSOR_TEST);
    display_scissor(160, 120, 320, 240);
    GLint clip[4]; glGetIntegerv(GL_SCISSOR_BOX, clip);
    assert(clip[0] == fit.left + (fit.right - fit.left) / 4);
    assert(clip[1] == fit.top + (fit.bottom - fit.top) / 4);
    display_scissor(0, 0, 640, 480);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    /* The bar clear must restore color mask, clear color and scissor state. */
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glClearColor(0.25f, 0.5f, 0.75f, 1);
    gfdm_display_present(dc);
    GLfloat color[4]; GLboolean mask[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, color); glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    assert(color[0] == .25f && color[1] == .5f && color[2] == .75f);
    assert(!mask[0] && mask[1] && !mask[2] && mask[3] && glIsEnabled(GL_SCISSOR_TEST));
    glGetIntegerv(GL_SCISSOR_BOX, clip);
    assert(clip[0] == fit.left && clip[1] == fit.top);
    pixel((fit.left + fit.right) / 2, (fit.top + fit.bottom) / 2, 255, 0, 0);
    if (fit.left) pixel(0, client.bottom / 2, 0, 0, 0);
    if (fit.top) pixel(client.right / 2, 0, 0, 0, 0);
    assert(glGetError() == GL_NO_ERROR);
    SendMessageW(window, WM_SYSKEYDOWN, VK_RETURN, (LPARAM) 1 << 29);
    assert(!display_fullscreen);
    GetWindowRect(window, &bounds); equal_rect(original, bounds);
    assert(GetWindowLongPtrW(window, GWL_STYLE) == style);
    display_viewport(0, 0, 640, 480);
    glGetIntegerv(GL_VIEWPORT, viewport);
    assert(viewport[2] == 640 && viewport[3] == 480);
    wglMakeCurrent(NULL, NULL); wglDeleteContext(context); ReleaseDC(window, dc);
    DestroyWindow(window); assert(display_window == NULL);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    puts("PASS: real WGL viewport/scissor, black bars, state preservation, Alt+Enter/repeat and original window restore");
    return 0;
}
