#ifndef GFDMHOOK1_MODS_H
#define GFDMHOOK1_MODS_H
#include <stdbool.h>
#include <stddef.h>
bool gfdm_mods_init(void);
bool gfdm_mods_enabled(void);
bool gfdm_mods_mount(void);
bool gfdm_mods_resolve(const char *name, char *output, size_t capacity);
void gfdm_mods_apply_hooks(void);
#endif
