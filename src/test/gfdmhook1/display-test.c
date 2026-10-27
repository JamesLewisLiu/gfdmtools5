#undef NDEBUG
#define LOG_SUPPRESS
#include "hook/table.h"
#include <GL/gl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static HMODULE test_module(const char *name);
static FARPROC test_export(HMODULE module, const char *name);
static void
test_hook(HMODULE, const char *, const struct hook_symbol *, size_t);
static BOOL WINAPI test_position(HWND, HWND, int, int, int, int, UINT);
#define GetModuleHandleA test_module
#define GetProcAddress test_export
#define hook_table_apply test_hook
#define SetWindowPos test_position
#include "gfdmhook1/display.c"
#undef GetModuleHandleA
#undef GetProcAddress
#undef hook_table_apply
#undef SetWindowPos

static int window_calls, full_calls, requests, restores, native_fullscreen;
static int full_success;
static bool exports_ready;
static DEVMODEW original;

static LONG WINAPI change_w(DEVMODEW *mode, DWORD flags)
{
    assert(mode != NULL);
    if (flags & CDS_FULLSCREEN) {
        requests++;
        assert(mode->dmPelsWidth == original.dmPelsWidth);
        assert(mode->dmPelsHeight == original.dmPelsHeight);
        return full_success ? DISP_CHANGE_SUCCESSFUL : DISP_CHANGE_BADMODE;
    }
    restores++;
    assert(mode->dmPelsWidth == original.dmPelsWidth);
    assert(mode->dmPelsHeight == original.dmPelsHeight);
    return DISP_CHANGE_SUCCESSFUL;
}
static int __cdecl windowscreen(void)
{
    window_calls++;
    if (native_fullscreen)
        display_change_w(&original, 0);
    native_fullscreen = 0;
    return 1;
}
static int __cdecl fullscreen(void)
{
    DEVMODEW request = {
        .dmSize = sizeof(request), .dmPelsWidth = 640, .dmPelsHeight = 480};
    full_calls++;
    assert(!native_fullscreen);
    native_fullscreen =
        display_change_w(&request, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
    return native_fullscreen;
}
static HMODULE test_module(const char *name)
{
    return name && strcmp(name, "libsystem.dll") == 0 ? (HMODULE) 1 :
                                                        GetModuleHandleA(name);
}
static FARPROC test_export(HMODULE module, const char *name)
{
    if (module != (HMODULE) 1)
        return GetProcAddress(module, name);
    if (!exports_ready)
        return NULL;
    if (!strcmp(name, "sys_gl_set_fullscreen"))
        return (FARPROC) (void *) fullscreen;
    assert(!strcmp(name, "sys_gl_set_windowscreen"));
    return (FARPROC) (void *) windowscreen;
}
static void test_hook(
    HMODULE module, const char *dll, const struct hook_symbol *syms, size_t n)
{
    if (module != (HMODULE) 1)
        hook_table_apply(module, dll, syms, n);
}
static BOOL WINAPI
test_position(HWND a, HWND b, int x, int y, int w, int h, UINT flags)
{
    return SetWindowPos(a, b, x, y, w, h, flags & ~SWP_SHOWWINDOW);
}
static HWND make_window(int w, int h)
{
    HWND win = CreateWindowExA(
        0,
        "GFDMDisplayTest",
        "Hidden display test",
        WS_POPUP,
        0,
        0,
        w,
        h,
        NULL,
        NULL,
        GetModuleHandleA(NULL),
        NULL);
    assert(win);
    return win;
}
static HGLRC make_context(HWND win, HDC *dc)
{
    *dc = GetDC(win);
    assert(*dc);
    PIXELFORMATDESCRIPTOR pf = {
        .nSize = sizeof(pf),
        .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 24,
        .cDepthBits = 16};
    int fmt = ChoosePixelFormat(*dc, &pf);
    assert(fmt && SetPixelFormat(*dc, fmt, &pf));
    HGLRC context = wglCreateContext(*dc);
    assert(context && wglMakeCurrent(*dc, context));
    return context;
}
static void pixel(int x, int y, int r, int g, int b)
{
    unsigned char p[4] = {0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    if (abs((int) p[0] - r) > 5 || abs((int) p[1] - g) > 5 ||
        abs((int) p[2] - b) > 5) {
        fprintf(
            stderr,
            "pixel %d,%d got %u,%u,%u expected %d,%d,%d\n",
            x,
            y,
            p[0],
            p[1],
            p[2],
            r,
            g,
            b);
        abort();
    }
}
static void draw(HWND win, HDC dc, bool stretch)
{
    assert(SetWindowPos(
        win, NULL, 0, 0, 1280, 720, SWP_NOACTIVATE | SWP_NOZORDER));
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, 640, 480);
    glScissor(0, 0, 640, 480);
    glEnable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 0, 480, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex2i(0, 0);
    glVertex2i(640, 0);
    glVertex2i(640, 480);
    glVertex2i(0, 480);
    glEnd();
    glScissor(160, 120, 320, 240);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLint before[4], after[4];
    GLfloat color_before[4], color_after[4];
    glGetIntegerv(GL_SCISSOR_BOX, before);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, color_before);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    gfdm_display_present(dc);
    GLboolean mask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    assert(!mask[0] && mask[1] && !mask[2] && mask[3]);
    glColorMask(1, 1, 1, 1);
    glGetIntegerv(GL_SCISSOR_BOX, after);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, color_after);
    assert(
        !memcmp(before, after, sizeof(before)) &&
        !memcmp(color_before, color_after, sizeof(color_before)));
    assert(glIsEnabled(GL_SCISSOR_TEST));
    glFinish();
    assert(glGetError() == GL_NO_ERROR);
    pixel(640, 360, 255, 0, 0);
    pixel(200, 40, 0, 0, 255);
    pixel(10, 360, 0, 0, stretch ? 255 : 0);
    glViewport(0, 0, -1, 480);
    assert(glGetError() == GL_INVALID_VALUE);
    glScissor(0, 0, -1, 480);
    assert(glGetError() == GL_INVALID_VALUE);
    /* A different window/context receives unmodified coordinates. */
    HWND helper = make_window(64, 64);
    HDC helper_dc;
    HGLRC helper_rc = make_context(helper, &helper_dc);
    glViewport(1, 2, 17, 19);
    glGetIntegerv(GL_VIEWPORT, after);
    assert(after[0] == 1 && after[1] == 2 && after[2] == 17 && after[3] == 19);
    gfdm_display_present(helper_dc);
    glGetIntegerv(GL_VIEWPORT, before);
    assert(!memcmp(before, after, sizeof(before)));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(helper_rc));
    ReleaseDC(helper, helper_dc);
    DestroyWindow(helper);
    assert(wglMakeCurrent(dc, display.context));
    /* Offscreen framebuffer calls also retain their own coordinates. */
    void(APIENTRY * gen)(GLsizei, GLuint *) =
        (void *) wglGetProcAddress("glGenFramebuffers");
    void(APIENTRY * bind)(GLenum, GLuint) =
        (void *) wglGetProcAddress("glBindFramebuffer");
    void(APIENTRY * del)(GLsizei, const GLuint *) =
        (void *) wglGetProcAddress("glDeleteFramebuffers");
    if ((uintptr_t) gen > 3 && (intptr_t) gen != -1 && (uintptr_t) bind > 3 &&
        (intptr_t) bind != -1 && del) {
        GLuint fbo;
        gen(1, &fbo);
        bind(0x8D40, fbo);
        glViewport(2, 3, 32, 33);
        glScissor(4, 5, 22, 23);
        glGetIntegerv(GL_VIEWPORT, before);
        gfdm_display_present(dc);
        glGetIntegerv(GL_VIEWPORT, after);
        assert(
            !memcmp(before, after, sizeof(before)) && after[2] == 32 &&
            after[3] == 33);
        bind(0x8D40, 0);
        del(1, &fbo);
        gfdm_display_present(dc);
        glGetIntegerv(GL_VIEWPORT, after);
        assert(after[2] == (stretch ? 1280 : 960) && after[3] == 720);
        puts(
            "PASS: offscreen framebuffer coordinates and main viewport "
            "restoration");
    } else {
        puts(
            "SKIP: optional framebuffer-object case is unsupported by this GL "
            "context");
    }
    assert(glGetError() == GL_NO_ERROR);
}
static void check_window(int mode, bool available, int success)
{
    HWND win = make_window(640, 480);
    HDC dc;
    HGLRC rc = make_context(win, &dc);
    window_calls = full_calls = requests = restores = native_fullscreen = 0;
    full_success = success;
    exports_ready = available;
    real_change_w = change_w;
    original.dmSize = sizeof(original);
    assert(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &original));
    gfdm_display_init(win, mode, 0);
    bool native = mode == 1 || mode == 2;
    bool scaled = mode >= 3 || (native && available && success);
    assert(native_fullscreen == (native && available && success));
    assert(window_calls == available && full_calls == (native && available));
    assert(requests == (native && available));
    RECT client;
    assert(GetClientRect(win, &client));
    LONG_PTR style = GetWindowLongPtr(win, GWL_STYLE);
    assert(!(GetWindowLongPtr(win, GWL_EXSTYLE) & WS_EX_TOPMOST));
    if (scaled) {
        assert((style & WS_POPUP) && !(style & WS_CAPTION));
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        assert(GetMonitorInfo(display.monitor, &monitor));
        assert(
            client.right == monitor.rcMonitor.right - monitor.rcMonitor.left);
        assert(
            client.bottom == monitor.rcMonitor.bottom - monitor.rcMonitor.top);
        draw(win, dc, mode == 2 || mode == 4);
        /* A deleted/recreated context is adopted before its first draw. */
        assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(rc));
        assert(display.context == NULL);
        rc = wglCreateContext(dc);
        assert(rc && wglMakeCurrent(dc, rc));
        assert(display.context == rc);
        GLint clip[4];
        glGetIntegerv(GL_SCISSOR_BOX, clip);
        assert(clip[2] == ((mode == 2 || mode == 4) ? 640 : 480));
        assert(clip[3] == 360);
        glViewport(0, 0, 640, 480);
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        assert(viewport[2] == ((mode == 2 || mode == 4) ? 1280 : 960));
        ShowWindow(win, SW_MINIMIZE);
        ShowWindow(win, SW_RESTORE);
        gfdm_display_present(dc);
    } else {
        assert(!(style & WS_POPUP) && ((style & WS_CAPTION) == WS_CAPTION));
        assert(client.right == 640 && client.bottom == 480);
    }
    if (native_fullscreen)
        windowscreen();
    assert(restores == (native && available && success));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(rc));
    ReleaseDC(win, dc);
    DestroyWindow(win);
    assert(display.window == NULL);
    printf("PASS mode %d available=%d success=%d\n", mode, available, success);
}
int main(void)
{
    WNDCLASSA cls = {
        .style = CS_OWNDC,
        .lpfnWndProc = DefWindowProcA,
        .hInstance = GetModuleHandleA(NULL),
        .lpszClassName = "GFDMDisplayTest"};
    assert(RegisterClassA(&cls));
    gfdm_display_apply_hooks();
    for (int i = 0; i <= 4; i++)
        check_window(i, true, 1);
    check_window(1, true, 0);
    check_window(2, false, 1);
    check_window(3, false, 1);
    check_window(4, false, 1);
    gfdm_display_init(NULL, 3, 0);
    assert(UnregisterClassA(cls.lpszClassName, cls.hInstance));
    puts(
        "PASS: five modes, native mode requests/restoration/fallback, real GL "
        "fit/stretch/clipping, black-bar state, helper/FBO isolation, context "
        "recreation and resize");
    return 0;
}
