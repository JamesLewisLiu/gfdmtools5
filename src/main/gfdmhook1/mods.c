/* Independent whole-file overlay. Search layout/priority reviewed against
   mon/ifs_layeredfs a0312f8; shared-data manifests are a separate concern. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfdmhook1/mods.h"
#include "hook/table.h"
#include "imports/avs.h"
#include "util/log.h"

#define MODS_MOUNT "/__gfdm_mods"
static char mods_root[MAX_PATH];
static bool mods_enabled, mods_mounted;
static int (__cdecl *mods_real_mount)(const char *, const char *, const char *, void *);
static int (__cdecl *mods_real_lstat)(const char *, struct avs_stat *);
static int (__cdecl *mods_real_convert)(char *, const char *);

static bool mods_relative(const char *name, char relative[MAX_PATH])
{
    if (!name) return false;
    if (!strncmp(name, "./", 2)) name += 2;
    else if (!strncmp(name, "../", 3)) name += 3;
    if (*name == '/' || *name == '\\') name++;
    if (_strnicmp(name, "data/", 5) && _strnicmp(name, "data\\", 5)) return false;
    name += 5;
    size_t length = strlen(name);
    if (!length || length >= MAX_PATH) return false;
    for (size_t i = 0; i <= length; i++)
        relative[i] = name[i] == '\\' ? '/' : name[i];
    /* Metadata and backing objects belong to the verified base store. */
    if (!_strnicmp(relative, "_loose/", 7) ||
        !_strnicmp(relative, "pack/", 5) ||
        !_stricmp(relative, "finfolist.bin")) return false;
    char *segment = relative;
    for (char *p = relative;; p++) {
        if (*p == ':' || *p == '*' || *p == '?' || *p == '"') return false;
        if (*p == '/' || !*p) {
            size_t n = (size_t)(p - segment);
            if (!n || (n == 1 && segment[0] == '.') ||
                (n == 2 && segment[0] == '.' && segment[1] == '.')) return false;
            if (!*p) break;
            segment = p + 1;
        }
    }
    return true;
}

bool gfdm_mods_init(void)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!n || n >= sizeof(exe)) return false;
    char *slash = strrchr(exe, '\\');
    if (!slash) return false;
    *slash = 0;
    if (snprintf(mods_root, sizeof(mods_root), "%s/data_mods", exe) >= sizeof(mods_root)) return false;
    DWORD attr = GetFileAttributesA(mods_root);
    mods_enabled = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    mods_mounted = false;
    if (!mods_enabled) return true;
    HMODULE avs = GetModuleHandleA("libavs-win32.dll");
    mods_real_mount = (void *) GetProcAddress(avs, "avs_fs_mount");
    mods_real_lstat = (void *) GetProcAddress(avs, "avs_fs_lstat");
    mods_real_convert = (void *) GetProcAddress(avs, "avs_fs_convert_path");
    if (!mods_real_mount || !mods_real_lstat || !mods_real_convert) return false;
    log_info("GFDM data_mods: %s (independent of shared data)", mods_root);
    return true;
}

bool gfdm_mods_enabled(void) { return mods_enabled; }

bool gfdm_mods_mount(void)
{
    if (!mods_enabled) return true;
    mods_mounted = mods_real_mount(MODS_MOUNT, mods_root, "fs", NULL) >= 0;
    return mods_mounted;
}

bool gfdm_mods_resolve(const char *name, char *output, size_t capacity)
{
    char relative[MAX_PATH], search[MAX_PATH], candidate[MAX_PATH];
    char selected[MAX_PATH] = "";
    WIN32_FIND_DATAA item;
    if (!mods_enabled || !mods_mounted || !mods_relative(name, relative)) return false;
    if (snprintf(search, sizeof(search), "%s/*", mods_root) >= sizeof(search)) return false;
    HANDLE find = FindFirstFileA(search, &item);
    if (find == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !strcmp(item.cFileName, ".") || !strcmp(item.cFileName, "..") ||
            !_stricmp(item.cFileName, "_cache")) continue;
        if (selected[0] && _stricmp(selected, item.cFileName) <= 0) continue;
        int n = snprintf(candidate, sizeof(candidate), "%s/%s/%s", mods_root, item.cFileName, relative);
        if (n < 0 || n >= sizeof(candidate)) continue;
        DWORD attr = GetFileAttributesA(candidate);
        if (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            strcpy(selected, item.cFileName);
    } while (FindNextFileA(find, &item));
    FindClose(find);
    if (!selected[0]) return false;
    int n = snprintf(output, capacity, MODS_MOUNT "/%s/%s", selected, relative);
    return n >= 0 && (size_t)n < capacity;
}

static int __cdecl mods_lstat(const char *name, struct avs_stat *stat)
{
    char path[MAX_PATH];
    return mods_real_lstat(gfdm_mods_resolve(name, path, sizeof(path)) ? path : name, stat);
}

static int __cdecl mods_convert(char *output, const char *name)
{
    char path[MAX_PATH];
    return mods_real_convert(output, gfdm_mods_resolve(name, path, sizeof(path)) ? path : name);
}

void gfdm_mods_apply_hooks(void)
{
    static const struct hook_symbol symbols[] = {
        {.name="avs_fs_lstat", .patch=mods_lstat},
        {.name="avs_fs_convert_path", .patch=mods_convert},
    };
    if (mods_enabled) hook_table_apply(NULL, "libavs-win32.dll", symbols, 2);
}
