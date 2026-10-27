#ifndef CONFIG_GFDM_NETWORK_CONFIG_H
#define CONFIG_GFDM_NETWORK_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

bool gfdm_network_select_path(
    const wchar_t *executable,
    const char *mode,
    wchar_t *path,
    size_t count,
    unsigned int *version);
bool gfdm_network_write_config(
    FILE *src,
    FILE *dst,
    unsigned int version,
    const char *mode,
    const char *server,
    const char *adapter,
    bool keyboard,
    int display_mode);

#endif
