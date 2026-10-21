#ifndef GFDMHOOK1_STORAGE_H
#define GFDMHOOK1_STORAGE_H

#include <windows.h>
#include <stdbool.h>

bool gfdm_storage_init(HMODULE boot);
bool gfdm_storage_prepare(void);
const char *gfdm_storage_nvram(void);
const char *gfdm_storage_raw(void);

#endif
