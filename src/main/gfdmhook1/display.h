#ifndef GFDMHOOK1_DISPLAY_H
#define GFDMHOOK1_DISPLAY_H

#include <windows.h>
#include <stdbool.h>

void gfdm_display_init(bool fullscreen);
void gfdm_display_apply_hooks(void);
void gfdm_display_present(HDC dc);

#endif
