#ifndef GFDMHOOK1_DISPLAY_H
#define GFDMHOOK1_DISPLAY_H

#include "gfdmhook1/display-mode.h"
#include <stdbool.h>
#include <windows.h>

void gfdm_display_prepare(void);
void gfdm_display_init(HWND window, int mode, int refresh);
void gfdm_display_apply_hooks(void);
void gfdm_display_present(HDC dc);

#endif
