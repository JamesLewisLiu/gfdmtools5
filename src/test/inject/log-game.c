#include <windows.h>

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    HMODULE hook = GetModuleHandleA("gfdm-log-hook.dll");
    void (*emit)(void) =
        hook ? (void (*)(void)) GetProcAddress(hook, "fixture_emit") : NULL;
    if (!emit)
        return 1;
    emit();
    return 0;
}
