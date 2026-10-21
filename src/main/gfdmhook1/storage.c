#include <windows.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gfdmhook1/storage.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

static char gfdm_conf_path[MAX_PATH];
static char gfdm_nvram_path[MAX_PATH];
static char gfdm_raw_path[MAX_PATH];
static char gfdm_tmp_path[MAX_PATH];
static char gfdm_cache_path[MAX_PATH];
static char gfdm_proxy_path[MAX_PATH];
static BOOL (WINAPI *real_CreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
static int (__cdecl *real_avs_fs_mount)(
    const char *, const char *, const char *, void *);

static bool gfdm_storage_set_paths(const char *executable)
{
    const char *slash;
    const char *forward;
    size_t base_len;

    slash = strrchr(executable, '\\');
    forward = strrchr(executable, '/');
    if (slash == NULL || (forward != NULL && forward > slash)) {
        slash = forward;
    }
    if (slash == NULL) {
        return false;
    }
    base_len = slash - executable;
    if (base_len + sizeof("\\CACHE\\PROXY") > MAX_PATH) {
        return false;
    }
    snprintf(gfdm_conf_path, sizeof(gfdm_conf_path),
             "%.*s\\CONF", (int) base_len, executable);
    snprintf(gfdm_nvram_path, sizeof(gfdm_nvram_path),
             "%s\\NVRAM", gfdm_conf_path);
    snprintf(gfdm_raw_path, sizeof(gfdm_raw_path),
             "%s\\RAW", gfdm_conf_path);
    snprintf(gfdm_tmp_path, sizeof(gfdm_tmp_path),
             "%.*s\\TMP", (int) base_len, executable);
    snprintf(gfdm_cache_path, sizeof(gfdm_cache_path),
             "%.*s\\CACHE", (int) base_len, executable);
    snprintf(gfdm_proxy_path, sizeof(gfdm_proxy_path),
             "%s\\PROXY", gfdm_cache_path);
    return true;
}

static const char *gfdm_storage_redirect(const char *path)
{
    char normalized[32];
    const char *relative;
    size_t len;
    bool conf;
    bool cache;

    if (path == NULL) {
        return path;
    }
    len = strlen(path);
    if (len >= sizeof(normalized)) {
        return path;
    }
    for (size_t i = 0; i <= len; i++) {
        normalized[i] = path[i] == '\\' ? '/' : path[i];
    }
    if (len != 0 && normalized[len - 1] == '/') {
        normalized[--len] = '\0';
    }
    /* Native boot creates CONF before avs_boot, then TMP/CACHE afterward.
       Match only those exact legacy directories, never unrelated paths,
       other games' saves or arbitrary drive roots. */
    if (_strnicmp(normalized, "E:/", 3) == 0) {
        relative = normalized + 3;
        conf = true;
        cache = false;
    } else if (_strnicmp(normalized, "F:/", 3) == 0) {
        relative = normalized + 3;
        conf = false;
        cache = true;
    } else if (strncmp(normalized, "./", 2) == 0) {
        relative = normalized + 2;
        conf = true;
        cache = true;
    } else {
        return path;
    }
    if (conf && _stricmp(relative, "CONF") == 0) {
        return gfdm_conf_path;
    }
    if (conf && _stricmp(relative, "CONF/NVRAM") == 0) {
        return gfdm_nvram_path;
    }
    if (conf && _stricmp(relative, "CONF/RAW") == 0) {
        return gfdm_raw_path;
    }
    if (cache && _stricmp(relative, "TMP") == 0) {
        return gfdm_tmp_path;
    }
    if (cache && _stricmp(relative, "CACHE") == 0) {
        return gfdm_cache_path;
    }
    if (cache && _stricmp(relative, "CACHE/PROXY") == 0) {
        return gfdm_proxy_path;
    }
    return path;
}

static BOOL WINAPI gfdm_CreateDirectoryA(
    LPCSTR path, LPSECURITY_ATTRIBUTES attributes)
{
    const char *redirected = gfdm_storage_redirect(path);

    if (redirected != path) {
        log_info("GFDM save directory: %s -> %s", path, redirected);
    }
    /* Preserve the real result, SECURITY_ATTRIBUTES and GetLastError. */
    return real_CreateDirectoryA(redirected, attributes);
}

static const struct hook_symbol gfdm_storage_syms[] = {
    {.name = "CreateDirectoryA", .patch = gfdm_CreateDirectoryA},
};

static int __cdecl gfdm_storage_mount(
    const char *mountpoint, const char *device, const char *fstype, void *data)
{
    /* These mounts happen after avs_boot, separately from its psmap. */
    if (mountpoint != NULL) {
        if (strcmp(mountpoint, "/tmp/") == 0) {
            device = gfdm_tmp_path;
        } else if (strcmp(mountpoint, "/proxy/") == 0) {
            device = gfdm_proxy_path;
        }
        if (device == gfdm_tmp_path || device == gfdm_proxy_path) {
            log_info("GFDM AVS mount: %s -> %s", mountpoint, device);
        }
    }
    return real_avs_fs_mount(mountpoint, device, fstype, data);
}

static const struct hook_symbol gfdm_storage_avs_syms[] = {
    {.name = "avs_fs_mount", .patch = gfdm_storage_mount,
     .link = (void **) &real_avs_fs_mount},
};

bool gfdm_storage_init(HMODULE boot)
{
    char executable[MAX_PATH];
    DWORD size;

    size = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (boot == NULL || size == 0 || size >= sizeof(executable) ||
        !gfdm_storage_set_paths(executable)) {
        return false;
    }
    real_CreateDirectoryA = CreateDirectoryA;
    /* boot.dll owns the legacy path literals. Scope the Windows hook
       to this consumer; AVS receives the absolute devices separately. */
    hook_table_apply(boot, "kernel32.dll", gfdm_storage_syms,
                     lengthof(gfdm_storage_syms));
    hook_table_apply(boot, "libavs-win32.dll", gfdm_storage_avs_syms,
                     lengthof(gfdm_storage_avs_syms));
    return true;
}

bool gfdm_storage_prepare(void)
{
    const char *paths[] = {gfdm_conf_path, gfdm_nvram_path, gfdm_raw_path,
                          gfdm_tmp_path, gfdm_cache_path, gfdm_proxy_path};

    for (size_t i = 0; i < lengthof(paths); i++) {
        if (!real_CreateDirectoryA(paths[i], NULL)) {
            DWORD error = GetLastError();
            DWORD attributes = GetFileAttributesA(paths[i]);

            if (error != ERROR_ALREADY_EXISTS ||
                attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                log_warning("GFDM save directory unavailable: %s (error %lu)",
                            paths[i], (unsigned long) error);
                SetLastError(error);
                return false;
            }
        }
    }
    return true;
}

const char *gfdm_storage_nvram(void)
{
    return gfdm_nvram_path;
}

const char *gfdm_storage_raw(void)
{
    return gfdm_raw_path;
}
