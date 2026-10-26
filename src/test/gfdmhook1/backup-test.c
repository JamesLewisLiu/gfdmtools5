#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static HMODULE test_module(const char *name);
static void *test_export(HMODULE module, const char *name);
#define GetModuleHandleA test_module
#define GetProcAddress test_export
#include "gfdmhook1/backup.c"
#undef GetModuleHandleA
#undef GetProcAddress

static int clear_flag, error_flag, valid, save_errors, test_result;
static int last_value, writes, notices, last_notice;
static const char *last_name;
static bool modules_ready, exports_ready;
static unsigned int patches;

static int __cdecl setting_get(const char *name)
{
    if (!strcmp(name, "NVRAM_CLEAR"))
        return clear_flag;
    if (!strcmp(name, "NVRAM_ERROR"))
        return error_flag;
    return 0;
}

static int __cdecl setting_set(const char *name, int value)
{
    last_name = name;
    last_value = value;
    writes++;
    if (name != NULL && !strcmp(name, "NVRAM_CLEAR"))
        clear_flag = value;
    return 73; /* Preserve the native setter's result, not just its argument. */
}

static int __cdecl nvram_valid(void)
{
    return valid;
}
static uint16_t __cdecl nvram_save_errors(void)
{
    return save_errors;
}

static HMODULE test_module(const char *name)
{
    if (!modules_ready)
        return NULL;
    if (!strcmp(name, "game.dll"))
        return (HMODULE) 1;
    if (!strcmp(name, "libsystem.dll"))
        return (HMODULE) 2;
    assert(!strcmp(name, "libshare-pj.dll"));
    return (HMODULE) 3;
}

static void *test_export(HMODULE module, const char *name)
{
    if (!exports_ready)
        return NULL;
    if (module == (HMODULE) 2) {
        assert(!strcmp(name, "sys_setting_get_param"));
        return (void *) setting_get;
    }
    assert(module == (HMODULE) 3);
    if (!strcmp(name, "?nvram_is_valid@@YAHXZ"))
        return (void *) nvram_valid;
    assert(!strcmp(name, "?game_util_get_nvram_save_err_count@@YAGXZ"));
    return (void *) nvram_save_errors;
}

void hook_table_apply(
    HMODULE target,
    const char *depname,
    const struct hook_symbol *syms,
    size_t count)
{
    assert(target == (HMODULE) 1); /* Never change the libshare initializer. */
    assert(!strcmp(depname, "libsystem.dll") && count == 1);
    assert(!strcmp(syms[0].name, "sys_setting_set_param"));
    assert(syms[0].patch == gfdm_backup_setting_set);
    *syms[0].link = setting_set;
    patches++;
}

/* V4 game.dll 0x10087a70, state 6: TEST result bit 8 acknowledges the
   notice, enters TEST, and writes NVRAM_CLEAR=1 again. Re-entering state 6
   after leaving TEST repeats the notice unless that write is corrected. */
static bool native_consumer(int(__cdecl *set)(const char *, int))
{
    if (setting_get("NVRAM_CLEAR") == 1) {
        notices++;
        last_notice = 1;
        if (!(test_result & 8))
            return false;
        set("NVRAM_CLEAR", 1);
    } else if (setting_get("NVRAM_ERROR") == 1) {
        notices++;
        last_notice = 0;
        if (!(test_result & 8))
            return false;
    }
    return true;
}

static void first_start(void)
{
    /* Native libshare saves defaults and sets the notice before game.dll. */
    setting_set("NVRAM_CLEAR", 1);
    error_flag = save_errors = test_result = writes = notices = 0;
    valid = 1;
}

int main(void)
{
    modules_ready = exports_ready = true;
    for (unsigned int version = 5; version <= 8; version++)
        gfdm_backup_apply_hooks(version);
    assert(patches == 0);
    modules_ready = false;
    gfdm_backup_apply_hooks(4);
    assert(patches == 0);
    modules_ready = true;
    exports_ready = false;
    gfdm_backup_apply_hooks(4);
    assert(patches == 0);
    exports_ready = true;
    gfdm_backup_apply_hooks(4);
    assert(patches == 1 && real_setting_set == setting_set);

    first_start();
    assert(!native_consumer(setting_set) && clear_flag == 1 && writes == 0);
    test_result = 8;
    assert(native_consumer(setting_set) && clear_flag == 1);
    test_result = 0;
    assert(!native_consumer(setting_set) && notices == 3); /* Original loop. */

    first_start();
    assert(!native_consumer(gfdm_backup_setting_set));
    assert(clear_flag == 1 && notices == 1 && writes == 0); /* Show it once. */
    test_result = 8;
    assert(native_consumer(gfdm_backup_setting_set) && clear_flag == 0);
    test_result = 0;
    assert(native_consumer(gfdm_backup_setting_set) && notices == 2);

    for (int failure = 0; failure < 3; failure++) {
        first_start();
        valid = failure != 0;
        error_flag = failure == 1;
        save_errors = failure == 2;
        test_result = 8;
        assert(native_consumer(gfdm_backup_setting_set) && clear_flag == 1);
        test_result = 0;
        assert(!native_consumer(gfdm_backup_setting_set));
    }
    clear_flag = 0;
    error_flag = 1;
    assert(!native_consumer(gfdm_backup_setting_set) && last_notice == 0);

    first_start();
    assert(
        gfdm_backup_setting_set(NULL, 1) == 73 && last_name == NULL &&
        last_value == 1);
    assert(gfdm_backup_setting_set("NVRAM_ERROR", 1) == 73 && last_value == 1);
    assert(gfdm_backup_setting_set("NVRAM_CLEAR", 2) == 73 && last_value == 2);
    assert(gfdm_backup_setting_set("NVRAM_CLEAR", 0) == 73 && last_value == 0);
    native_nvram_is_valid = NULL;
    assert(gfdm_backup_setting_set("NVRAM_CLEAR", 1) == 73 && last_value == 1);
    native_nvram_is_valid = nvram_valid;
    native_setting_get = NULL;
    assert(gfdm_backup_setting_set("NVRAM_CLEAR", 1) == 73 && last_value == 1);
    native_setting_get = setting_get;
    native_save_errors = NULL;
    assert(gfdm_backup_setting_set("NVRAM_CLEAR", 1) == 73 && last_value == 1);

    puts(
        "PASS: fresh V4 notice, TEST acknowledgement without restart, "
        "invalid/save-error guards, original 2500(0), native return and "
        "V4-only consumer scope");
    return 0;
}
