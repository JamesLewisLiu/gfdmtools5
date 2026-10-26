#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "gfdmhook1/backup.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

static int(__cdecl *real_setting_set)(const char *, int);
static int(__cdecl *native_setting_get)(const char *);
static int(__cdecl *native_nvram_is_valid)(void);
static uint16_t(__cdecl *native_save_errors)(void);

static int __cdecl gfdm_backup_setting_set(const char *name, int value)
{
    /* This IAT hook is scoped to V4 game.dll. Its only NVRAM_CLEAR=1 write
       follows error_info_window_call_attendant(2500, 1) returning TEST (bit 8).
       libshare-pj's first-start initialization must still set the notice. */
    if (name != NULL && strcmp(name, "NVRAM_CLEAR") == 0 && value == 1 &&
        native_setting_get != NULL && native_nvram_is_valid != NULL &&
        native_save_errors != NULL) {
        if (native_nvram_is_valid() != 0 &&
            native_setting_get("NVRAM_ERROR") == 0 &&
            native_save_errors() == 0) {
            value = 0;
            log_info(
                "GFDM V4 backup initialization acknowledged; clearing "
                "first-start notice");
        } else {
            log_warning(
                "GFDM V4 backup data is not ready; keeping initialization "
                "notice");
        }
    }
    return real_setting_set(name, value);
}

static const struct hook_symbol gfdm_backup_syms[] = {
    {.name = "sys_setting_set_param",
     .patch = gfdm_backup_setting_set,
     .link = (void **) &real_setting_set},
};

void gfdm_backup_apply_hooks(unsigned int version)
{
    HMODULE game, system, share;

    if (version != 4) {
        return;
    }
    game = GetModuleHandleA("game.dll");
    system = GetModuleHandleA("libsystem.dll");
    share = GetModuleHandleA("libshare-pj.dll");
    if (game == NULL || system == NULL || share == NULL) {
        return;
    }
    native_setting_get =
        (void *) GetProcAddress(system, "sys_setting_get_param");
    native_nvram_is_valid =
        (void *) GetProcAddress(share, "?nvram_is_valid@@YAHXZ");
    native_save_errors = (void *) GetProcAddress(
        share, "?game_util_get_nvram_save_err_count@@YAGXZ");
    if (native_setting_get == NULL || native_nvram_is_valid == NULL ||
        native_save_errors == NULL) {
        return;
    }
    hook_table_apply(
        game, "libsystem.dll", gfdm_backup_syms, lengthof(gfdm_backup_syms));
}
