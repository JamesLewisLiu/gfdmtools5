#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bemanitools/input.h"
#include "cconfig/cconfig-hook.h"
#include "gfdmhook1/config.h"
#include "gfdmhook1/network.h"
#include "hook/iohook.h"
#include "hooklib/adapter.h"
#include "hook/table.h"
#include "imports/avs.h"
#include "p3io/cmd.h"
#include "p3ioemu/devmgr.h"
#include "p3ioemu/emu.h"
#include "security/rp-sign-key.h"
#include "security/rp3.h"
#include "util/defs.h"
#include "util/log.h"
#include "util/str.h"
#include "util/thread.h"

#define GFDMHOOK1_INFO_HEADER \
    "gfdmhook1 for GFDM V4, build " __DATE__ " " __TIME__ \
    ", gitrev " STRINGIFY(GITREV)
#define GFDMHOOK1_CMD_USAGE \
    "Usage: inject.exe gfdmhook1.dll gdv4.exe -g|-d [options...]"

static struct gfdmhook1_config gfdm_config;
static bool gfdm_initialized;
static bool gfdm_keyboard;
static bool gfdm_mapper_loaded;
static bool gfdm_is_gf;
static FILE *gfdm_log_file;
static HMODULE gfdm_module;
static struct security_mcode gfdm_mcode;
static struct security_id gfdm_pcbid;
static struct security_id gfdm_eamid;
static unsigned int(__cdecl *real_device_get_input)(int player);
static int(__cdecl *real_device_get_jamma_history)(
    void *history, int max_entries);
static HMODULE(STDCALL *real_LoadLibraryA)(LPCSTR name);
static int(__cdecl *real_movie_is_ready)(void);
static int(__cdecl *real_movie_is_ready_system)(void);
#if AVS_VERSION >= 1600
static void (*real_avs_boot)(
    struct property_node *config,
    void *com_heap,
    size_t sz_com_heap,
    void *reserved,
    avs_log_writer_t log_writer,
    void *log_context);
#else
static void (*real_avs_boot)(
    struct property_node *config,
    void *std_heap,
    size_t sz_std_heap,
    void *avs_heap,
    size_t sz_avs_heap,
    avs_log_writer_t log_writer,
    void *log_context);
#endif

static void gfdm_apply_device_hooks(HMODULE target);
static void gfdm_apply_extio_hooks(HMODULE target);
static void gfdm_apply_avs_hooks(HMODULE target);
static void gfdm_apply_movie_hooks(HMODULE target);

/* The injector passes the game selector as a separate command-line token
   ("-g" or "-d").  The old substring check was fragile when the command
   line was quoted by inject.exe and could silently select the DM mapper for a
   GF launch.  Parse the selector explicitly so the mapper and compact input
   translation always agree with the selected cabinet. */
static bool gfdm_command_has_switch(const char *cmdline, char wanted)
{
    const char *p;

    for (p = cmdline; p != NULL && *p != '\0';) {
        char token[16];
        size_t n;

        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        n = 0;
        if (*p == '"') {
            p++;
            while (*p != '\0' && *p != '"') {
                if (n + 1 < sizeof(token)) {
                    token[n++] = *p;
                }
                p++;
            }
            if (*p == '"') {
                p++;
            }
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                if (n + 1 < sizeof(token)) {
                    token[n++] = *p;
                }
                p++;
            }
        }

        token[n] = '\0';
        if ((n == 2 && token[0] == '-' && token[1] == wanted) ||
            (n == 2 && token[0] == '/' && token[1] == wanted)) {
            return true;
        }
    }

    return false;
}

static void gfdm_open_log_file(void)
{
    char module_path[MAX_PATH];
    char *separator;
    char log_path[MAX_PATH];

    if (gfdm_log_file != NULL) {
        return;
    }

    if (GetModuleFileNameA(gfdm_module, module_path,
                           lengthof(module_path)) > 0 &&
        (separator = strrchr(module_path, '\\')) != NULL) {
        separator[1] = '\0';
        str_format(log_path, sizeof(log_path),
                   "%sgfdm-v4-hook.log", module_path);
        gfdm_log_file = fopen(log_path, "a");
    }

    if (gfdm_log_file == NULL) {
        gfdm_log_file = fopen("gfdm-v4-hook.log", "a");
    }
}

static void gfdm_log_writer(void *ctx, const char *chars, size_t nchars)
{
    (void) ctx;

    OutputDebugStringA(chars);

    if (gfdm_log_file != NULL) {
        fwrite(chars, 1, nchars, gfdm_log_file);
        fflush(gfdm_log_file);
    }
}

/*
 * V4's libmovie has no null check for the decoder object in its exported
 * movie_is_ready() path.  When video setup fails part-way through, the
 * object field at +0x264 can contain a small error value (the crash dump had
 * 0x4); libmovie then treats it as a pointer and enters a critical section
 * at object + 0x1c2950.  Keep the original implementation for valid objects,
 * but report "not ready" while the object is absent or clearly invalid so
 * the game can continue without dereferencing the bad state.
 */
static bool gfdm_movie_readable(const void *address, size_t size)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t start;
    uintptr_t end;

    if (address == NULL || size == 0) {
        return false;
    }

    start = (uintptr_t) address;
    end = start + size;
    if (end < start) {
        return false;
    }

    if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT) {
        return false;
    }

    return end <= (uintptr_t) mbi.BaseAddress + mbi.RegionSize;
}

static int gfdm_movie_is_ready_call(int(__cdecl *original)(void))
{
    HMODULE movie;
    uintptr_t movie_base;
    uintptr_t object;
    uintptr_t decoder;

    movie = GetModuleHandleA("libmovie.dll");
    movie_base = (uintptr_t) movie;

    if (movie_base == 0 ||
        !gfdm_movie_readable((const void *) (movie_base + 0x1a81a0),
                             sizeof(uintptr_t))) {
        return 0;
    }

    object = *(const uintptr_t *) (movie_base + 0x1a81a0);
    if (object == 0) {
        return 0;
    }

    if (!gfdm_movie_readable((const void *) object, 0x268)) {
        return 0;
    }

    decoder = *(const uintptr_t *) (object + 0x264);

    /* The invalid state seen in gdv4.exe.39856.dmp was exactly 0x4. */
    if (decoder < 0x10000 ||
        !gfdm_movie_readable((const void *) decoder, 1) ||
        !gfdm_movie_readable((const void *) (decoder + 0x1c296d), 1)) {
        log_warning("V4 movie decoder state is invalid (%p); reporting not ready",
                    (void *) decoder);
        return 0;
    }

    if (original == NULL) {
        return 0;
    }

    return original();
}

static int __cdecl gfdm_movie_is_ready(void)
{
    return gfdm_movie_is_ready_call(real_movie_is_ready);
}

static int __cdecl gfdm_movie_is_ready_system(void)
{
    return gfdm_movie_is_ready_call(real_movie_is_ready_system);
}

static const struct hook_symbol gfdm_movie_syms[] = {
    {
        .name = "?movie_is_ready@@YAHXZ",
        .patch = gfdm_movie_is_ready,
        .link = (void **) &real_movie_is_ready,
    },
    {
        .name = "?movie_is_ready_system@@YAHXZ",
        .patch = gfdm_movie_is_ready_system,
        .link = (void **) &real_movie_is_ready_system,
    },
};

/* V4's libextio expects the physical IC-card unit to be present during the
   boot-time CARDUNIT_CHECK pass.  The PC launch has no such serial device.
   Do not let the original cardunit_boot create its serial worker: that worker
   continues to run after the check and eventually enters AVS with an invalid
   synchronization object.  Keep the complete exported card-unit surface as a
   small, idle reader instead (the same contract used by Gitadora tools). */
static void __cdecl gfdm_cardunit_boot(int unit_count, int reserved)
{
    (void) unit_count;
    (void) reserved;
}

static int __cdecl gfdm_cardunit_boot_initialize(void)
{
    return 0;
}

static void __cdecl gfdm_cardunit_update(void)
{
}

static void __cdecl gfdm_cardunit_card_eject(int unit_no)
{
    (void) unit_no;
}

static int __cdecl gfdm_cardunit_card_eject_wait(int unit_no)
{
    (void) unit_no;

    return 1;
}

static int __cdecl gfdm_cardunit_get_errorcount(int unit_no)
{
    (void) unit_no;

    return 0;
}

static int __cdecl gfdm_cardunit_get_status(int unit_no)
{
    log_misc("V4 cardunit %d status -> ready", unit_no);

    return 1;
}

static int __cdecl gfdm_cardunit_card_sensor(int unit_no)
{
    (void) unit_no;

    return 0;
}

static int __cdecl gfdm_cardunit_card_sensor_raw(int unit_no)
{
    (void) unit_no;

    /* Gitadora's dummy reader reports a connected sensor with no card. */
    return 1;
}

static int __cdecl gfdm_cardunit_card_eject_complete(int unit_no)
{
    (void) unit_no;

    return 1;
}

static int __cdecl gfdm_cardunit_card_read(int unit_no, void *card)
{
    (void) unit_no;

    if (card != NULL) {
        memset(card, 0, 8);
    }

    /* No card is inserted. */
    return 1;
}

static void __cdecl gfdm_cardunit_card_ready(int unit_no)
{
    (void) unit_no;
}

static int __cdecl gfdm_cardunit_key_get(int unit_no)
{
    (void) unit_no;

    return -1;
}

static const char *__cdecl gfdm_cardunit_key_str(int unit_no)
{
    (void) unit_no;

    return "";
}

static int __cdecl gfdm_cardunit_reset(void)
{
    return 0;
}

static void __cdecl gfdm_cardunit_shutdown(void)
{
}

static const void *__cdecl gfdm_cardunit_get_version(int unit_no)
{
    static const uint8_t version[] = {'D', 'U', 'M', 'M', 'Y', 4, 2, 0};

    (void) unit_no;

    return version;
}

static const struct hook_symbol gfdm_extio_syms[] = {
    {
        .name = "?cardunit_boot@@YAXHH@Z",
        .patch = gfdm_cardunit_boot,
    },
    {
        .name = "?cardunit_boot_initialize@@YAHXZ",
        .patch = gfdm_cardunit_boot_initialize,
    },
    {
        .name = "?cardunit_update@@YAXXZ",
        .patch = gfdm_cardunit_update,
    },
    {
        .name = "?cardunit_card_eject@@YAXH@Z",
        .patch = gfdm_cardunit_card_eject,
    },
    {
        .name = "?cardunit_card_eject_wait@@YAHH@Z",
        .patch = gfdm_cardunit_card_eject_wait,
    },
    {
        .name = "?cardunit_card_read@@YAHHQAE@Z",
        .patch = gfdm_cardunit_card_read,
    },
    {
        .name = "?cardunit_card_ready@@YAXH@Z",
        .patch = gfdm_cardunit_card_ready,
    },
    {
        .name = "?cardunit_get_errorcount@@YAHH@Z",
        .patch = gfdm_cardunit_get_errorcount,
    },
    {
        .name = "?cardunit_get_status@@YAHH@Z",
        .patch = gfdm_cardunit_get_status,
    },
    {
        .name = "?cardunit_card_sensor@@YAHH@Z",
        .patch = gfdm_cardunit_card_sensor,
    },
    {
        .name = "?cardunit_card_sensor_raw@@YAHH@Z",
        .patch = gfdm_cardunit_card_sensor_raw,
    },
    {
        .name = "?cardunit_card_eject_complete@@YAHH@Z",
        .patch = gfdm_cardunit_card_eject_complete,
    },
    {
        .name = "?cardunit_key_get@@YAHH@Z",
        .patch = gfdm_cardunit_key_get,
    },
    {
        .name = "?cardunit_key_str@@YAPBDH@Z",
        .patch = gfdm_cardunit_key_str,
    },
    {
        .name = "?cardunit_reset@@YAHXZ",
        .patch = gfdm_cardunit_reset,
    },
    {
        .name = "?cardunit_shutdown@@YAXXZ",
        .patch = gfdm_cardunit_shutdown,
    },
    {
        .name = "?cardunit_get_version@@YAPBUfirm_version@@H@Z",
        .patch = gfdm_cardunit_get_version,
    },
};

static void gfdm_replace_property_str(
    struct property_node *root, const char *path, const char *value)
{
    struct property_node *node;

    node = property_search(NULL, root, path);
    if (node != NULL) {
        property_node_remove(node);
    }

    node = property_node_create(
        NULL, root, PROPERTY_TYPE_STR, path, value);
    if (node != NULL) {
        property_node_datasize(node);
    }
}

#if AVS_VERSION >= 1600
static void __cdecl gfdm_avs_boot(
    struct property_node *config,
    void *com_heap,
    size_t sz_com_heap,
    void *reserved,
    avs_log_writer_t log_writer,
    void *log_context)
#else
static void __cdecl gfdm_avs_boot(
    struct property_node *config,
    void *std_heap,
    size_t sz_std_heap,
    void *avs_heap,
    size_t sz_avs_heap,
    avs_log_writer_t log_writer,
    void *log_context)
#endif
{
    char base[MAX_PATH];
    char nvram[MAX_PATH];
    char raw[MAX_PATH];
    char *slash;

    memset(base, 0, sizeof(base));
    GetModuleFileNameA(NULL, base, sizeof(base) - 1);
    slash = strrchr(base, '\\');
    if (slash != NULL) {
        *slash = '\0';
    }

    snprintf(nvram, sizeof(nvram), "%s\\CONF\\NVRAM", base);
    snprintf(raw, sizeof(raw), "%s\\CONF\\RAW", base);

    {
        char conf[MAX_PATH];

        snprintf(conf, sizeof(conf), "%s\\CONF", base);
        CreateDirectoryA(conf, NULL);
    }
    CreateDirectoryA(nvram, NULL);
    CreateDirectoryA(raw, NULL);

    gfdm_replace_property_str(config, "/fs/nvram/device", nvram);
    gfdm_replace_property_str(config, "/fs/raw/device", raw);
    log_info("V4 AVS paths: nvram=%s raw=%s", nvram, raw);

    if (real_avs_boot != NULL) {
#if AVS_VERSION >= 1600
        real_avs_boot(
            config,
            com_heap,
            sz_com_heap,
            reserved,
            log_writer,
            log_context);
#else
        real_avs_boot(
            config,
            std_heap,
            sz_std_heap,
            avs_heap,
            sz_avs_heap,
            log_writer_debug,
            NULL);
#endif
    }
}

static HMODULE STDCALL gfdm_LoadLibraryA(LPCSTR name)
{
    HMODULE module;

    module = GetModuleHandleA(name);
    if (module == NULL && real_LoadLibraryA != NULL) {
        module = real_LoadLibraryA(name);
    }

    if (module != NULL) {
        /* The V4 system/error libraries are loaded after boot_main. */
        gfdm_apply_device_hooks(module);
        gfdm_apply_extio_hooks(module);
        gfdm_apply_avs_hooks(NULL);
        /* The import we need to patch lives in game.dll, not libmovie.dll.
           Re-scan all loaded modules after late movie loading so the game's
           IAT is updated even when game.dll was loaded before libmovie. */
        gfdm_apply_movie_hooks(NULL);
    }

    return module;
}

/*
 * GFDM V4's original gfdmhook exposes the libdevice security entry points
 * directly.  In particular, device_check_secplug() returns the status word
 * expected by the game (0x101 for the black plug and 0x100 for the white
 * plug).  V4 does not treat a boolean "present" value as a connected plug;
 * returning 1 here therefore still produces the 5-1503-0001 error before the
 * P3IO roundplug data is examined.
 *
 * Keep the P3IO emulation below as the source of the ROM/EEPROM data, but
 * provide the same compatibility entry points as the known-good Gitadora
 * hook.  This also makes the result independent of whether a particular V4
 * libdevice build performs the check through its P3IO worker or through the
 * exported game-facing API.
 */
static int __cdecl gfdm_device_check_secplug(int plug_id)
{
    int status;

    status = plug_id == 0 ? 0x101 : 0x100;
    log_misc("Security plug check %d -> 0x%03X", plug_id, status);

    return status;
}

static int __cdecl gfdm_device_get_secplug(
    int plug_id, uint8_t *rom, uint8_t *eeprom)
{
    (void) plug_id;
    (void) rom;
    (void) eeprom;

    return 1;
}

static int __cdecl gfdm_device_read_secplug(
    int plug_id, uint8_t *rom, uint8_t *eeprom)
{
    (void) plug_id;
    (void) rom;
    (void) eeprom;

    return 1;
}

static void __cdecl gfdm_device_update_secplug(void)
{
}

static int __cdecl gfdm_device_get_jamma_history(
    void *history, int max_entries);
static unsigned int __cdecl gfdm_device_get_input(int player);

static const struct hook_symbol gfdm_secplug_syms[] = {
    {
        .name = "?device_check_secplug@@YAHH@Z",
        .patch = gfdm_device_check_secplug,
    },
    {
        .name = "?device_get_secplug@@YAHHQAE0@Z",
        .patch = gfdm_device_get_secplug,
    },
    {
        .name = "?device_read_secplug@@YAHHQAE0@Z",
        .patch = gfdm_device_read_secplug,
    },
    {
        .name = "?device_update_secplug@@YAXXZ",
        .patch = gfdm_device_update_secplug,
    },
    {
        .name = "?device_get_input@@YAIH@Z",
        .patch = gfdm_device_get_input,
        .link = (void **) &real_device_get_input,
    },
    {
        .name = "?device_get_jamma_history@@YAHPAUT_JAMMA_HISTORY_INFO@@H@Z",
        .patch = gfdm_device_get_jamma_history,
        .link = (void **) &real_device_get_jamma_history,
    },
};

static const struct hook_symbol gfdm_loader_syms[] = {
    {
        .name = "LoadLibraryA",
        .patch = gfdm_LoadLibraryA,
        .link = (void **) &real_LoadLibraryA,
    },
};

static const struct hook_symbol gfdm_avs_syms[] = {
    {
        .name = "avs_boot",
        .patch = gfdm_avs_boot,
        .link = (void **) &real_avs_boot,
    },
};

static void gfdm_apply_device_hooks(HMODULE target)
{
    hook_table_apply(target, "libdevice.dll", gfdm_secplug_syms,
                     lengthof(gfdm_secplug_syms));
    hook_table_apply(target, "device.dll", gfdm_secplug_syms,
                     lengthof(gfdm_secplug_syms));
}

static void gfdm_apply_extio_hooks(HMODULE target)
{
    hook_table_apply(target, "libextio.dll", gfdm_extio_syms,
                     lengthof(gfdm_extio_syms));
}

static void gfdm_apply_avs_hooks(HMODULE target)
{
    hook_table_apply(target, "libavs-win32.dll", gfdm_avs_syms,
                     lengthof(gfdm_avs_syms));
}

static void gfdm_apply_movie_hooks(HMODULE target)
{
    hook_table_apply(target, "libmovie.dll", gfdm_movie_syms,
                     lengthof(gfdm_movie_syms));
}

static void gfdm_read_keys(uint32_t *state)
{
    *state = 0;

    if (!gfdm_keyboard) {
        return;
    }

    /* P3IO input is active-high. This fallback keeps the hook usable before
       the optional generic-input mapping has been configured. */
    if (GetAsyncKeyState(VK_F1) & 0x8000) *state |= 1u << 0; /* service */
    if (GetAsyncKeyState(VK_F2) & 0x8000) *state |= 1u << 1; /* test */

    if (gfdm_is_gf) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
            *state |= 1u << 8;
            *state |= 1u << 9;
        }
        if (GetAsyncKeyState('Z') & 0x8000) *state |= 1u << 18;
        if (GetAsyncKeyState('X') & 0x8000) *state |= 1u << 20;
        if (GetAsyncKeyState('C') & 0x8000) *state |= 1u << 22;
        if (GetAsyncKeyState('A') & 0x8000) *state |= 1u << 24;
        if (GetAsyncKeyState('S') & 0x8000) *state |= 1u << 25;
        if (GetAsyncKeyState('Q') & 0x8000) *state |= 1u << 12;
        if (GetAsyncKeyState('W') & 0x8000) *state |= 1u << 28;
    } else {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) *state |= 1u << 8;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) *state |= 1u << 15;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) *state |= 1u << 17;
        if (GetAsyncKeyState('Z') & 0x8000) *state |= 1u << 10;
        if (GetAsyncKeyState('X') & 0x8000) *state |= 1u << 12;
        if (GetAsyncKeyState('C') & 0x8000) *state |= 1u << 14;
        if (GetAsyncKeyState('V') & 0x8000) *state |= 1u << 16;
        if (GetAsyncKeyState('B') & 0x8000) *state |= 1u << 18;
        if (GetAsyncKeyState('N') & 0x8000) *state |= 1u << 22;
    }
}

static uint32_t gfdm_read_input_state(void)
{
    static bool first_update = true;
    static uint32_t last_state;
    uint32_t state;
    uint32_t keyboard_state;

    state = gfdm_mapper_loaded ? (uint32_t) mapper_update() : 0;

    /* The keyboard mapping is an explicit fallback.  In particular, do not
       merge it into the mapper state when input.keyboard=false: doing so made
       it impossible to tell whether a configured HID action was reaching the
       game. */
    if (gfdm_keyboard) {
        gfdm_read_keys(&keyboard_state);
        state |= keyboard_state;
    }

    if (first_update || state != last_state) {
        log_misc(
            "GFDM input state: %08x (%s mapper)",
            state,
            gfdm_mapper_loaded ? "configured" : "keyboard");
        last_state = state;
        first_update = false;
    }

    if (!gfdm_mapper_loaded) {
        return state;
    }

    return state;
}

/*
 * V4's game-facing libdevice API uses the compact input word returned by
 * device_get_input(), rather than the physical P3IO bit positions exposed by
 * read_jamma().  The stock V4 libdevice has no useful host input source in a
 * PC launch, so simply emulating P3IO is not enough to make TEST/SERVICE
 * usable on the backup-error screen.  Translate the same mapper state used
 * by the P3IO path into the libdevice word as Gitadora's hook does.
 */
static unsigned int __cdecl gfdm_device_get_input(int player)
{
    uint32_t state;
    unsigned int result;

    state = gfdm_read_input_state();
    result = 0;

    if (state & (1u << 0)) result |= 0x08; /* SERVICE */
    if (state & (1u << 1)) result |= 0x02; /* TEST */

    if (gfdm_is_gf) {
        if (player != 1) {
            if (state & (1u << 8)) result |= 0x0004;  /* START */
            if (state & (1u << 12)) result |= 0x0040; /* WAIL (DOWN) */
            if (state & (1u << 18)) result |= 0x0200; /* RED (BUTTON0) */
            if (state & (1u << 20)) result |= 0x0400; /* GREEN (BUTTON1) */
            if (state & (1u << 22)) result |= 0x0800; /* BLUE (BUTTON2) */
            if (state & (1u << 24)) result |= 0x0020; /* PICK A (UP) */
            if (state & (1u << 25)) result |= 0x0040; /* PICK B (DOWN) */
            if (state & (1u << 28)) result |= 0x8000; /* EFFECTOR (LEFT) */
        } else {
            if (state & (1u << 9)) result |= 0x0004;
            if (state & (1u << 13)) result |= 0x0040;
            if (state & (1u << 19)) result |= 0x0200;
            if (state & (1u << 21)) result |= 0x0400;
            if (state & (1u << 23)) result |= 0x0800;
            if (state & (1u << 26)) result |= 0x0020;
            if (state & (1u << 27)) result |= 0x0040;
            if (state & (1u << 29)) result |= 0x8000;
        }
    } else {
        if (state & (1u << 8)) result |= 0x0004;  /* START */
        if (state & (1u << 15)) result |= 0x0080; /* MENU LEFT */
        if (state & (1u << 17)) result |= 0x0100; /* MENU RIGHT */
        if (state & (1u << 10)) result |= 0x0020; /* HI-HAT (UP) */
        if (state & (1u << 12)) result |= 0x0040; /* SNARE (DOWN) */
        if (state & (1u << 14)) result |= 0x0080; /* HIGH TOM */
        if (state & (1u << 16)) result |= 0x0100; /* LOW TOM */
        if (state & (1u << 18)) result |= 0x0200; /* CYMBAL (BUTTON0) */
        if (state & (1u << 22)) result |= 0x0800; /* BASS (BUTTON2) */
    }

    /* V4 exposes the two debug switches as global input bits rather than as
       player buttons.  They are nevertheless returned by device_get_input,
       so both player queries see the same configured switch state. */
    if (state & (1u << 30)) result |= 0x00020000; /* DEBUG MENU SELECT */
    if (state & (1u << 31)) result |= 0x00040000; /* DEBUG MENU DECIDE */

    if (result != 0) {
        log_misc("V4 device input player %d: %08x", player, result);
    }

    return result;
}

static int __cdecl gfdm_device_get_jamma_history(
    void *history, int max_entries)
{
    uint32_t state;
    uint8_t *entry;
    uint32_t tick;
    int result;

    result = real_device_get_jamma_history != NULL
        ? real_device_get_jamma_history(history, max_entries)
        : 0;

    if (history == NULL || max_entries <= 0) {
        return result;
    }

    state = gfdm_read_input_state();

    /* The V4 error screen consumes the TEST edge from JAMMA history. If the
       real device queue contains unrelated entries, replace it while TEST is
       held so the screen cannot discard the mapped key as stale input. */
    if ((state & (1u << 1)) != 0) {
        entry = (uint8_t *) history;
        memset(entry, 0, 12);
        tick = GetTickCount();
        memcpy(entry, &tick, sizeof(tick));
        memcpy(entry + 4, &tick, sizeof(tick));
        entry[8] = 0x20;

        log_misc("Synthesized V4 JAMMA history for TEST");
        return 1;
    }

    if (result > 0 || (state & (1u << 0)) == 0) {
        return result;
    }

    entry = (uint8_t *) history;
    memset(entry, 0, 12);
    tick = GetTickCount();
    memcpy(entry, &tick, sizeof(tick));
    memcpy(entry + 4, &tick, sizeof(tick));

    /* History bit 5 is V4's TEST edge; bit 6 is SERVICE. */
    *(uint16_t *) (entry + 8) = state & (1u << 1) ? 0x20 : 0x40;

    log_misc(
        "Synthesized V4 JAMMA history for %s",
        state & (1u << 1) ? "TEST" : "SERVICE");

    return 1;
}

static HRESULT gfdm_read_jamma(void *ctx, uint32_t *state)
{
    (void) ctx;

    *state = gfdm_read_input_state();

    return S_OK;
}

static HRESULT gfdm_set_outputs(void *ctx, uint32_t state)
{
    (void) ctx;
    log_misc("P3IO outputs: %08x", state);
    return S_OK;
}

static HRESULT gfdm_get_dipsw(void *ctx, uint8_t *state)
{
    (void) ctx;
    *state = 0;
    return S_OK;
}

static HRESULT gfdm_get_cab_type(void *ctx, enum p3io_cab_type *type)
{
    (void) ctx;
    *type = P3IO_CAB_TYPE_HD;
    return S_OK;
}

static HRESULT gfdm_get_video_freq(void *ctx, enum p3io_video_freq *freq)
{
    (void) ctx;
    *freq = P3IO_VIDEO_FREQ_31KHZ;
    return S_OK;
}

static HRESULT gfdm_get_coinstock(void *ctx, uint16_t *slots, size_t nslots)
{
    (void) ctx;
    for (size_t i = 0; i < nslots; i++) slots[i] = 0;
    return S_OK;
}

static HRESULT gfdm_get_roundplug(
    void *ctx, uint8_t plug_id, uint8_t *rom, uint8_t *eeprom)
{
    struct security_rp3_eeprom data;
    const struct security_id *id;
    const struct security_mcode *mcode;

    (void) ctx;
    id = plug_id == 0 ? &gfdm_pcbid : &gfdm_eamid;
    mcode = plug_id == 0 ? &gfdm_mcode : &security_mcode_eamuse;

    if (plug_id == 0) {
        memcpy(rom, id->id, sizeof(id->id));
    } else {
        /* GFDM checks the white plug's ROM for the eight '@' bytes. The
           EAMID is carried in the signed EEPROM payload instead. */
        memset(rom, '@', sizeof(id->id));
    }
    security_rp3_generate_signed_eeprom_data(
        plug_id == 0 ? SECURITY_RP_UTIL_RP_TYPE_BLACK
                     : SECURITY_RP_UTIL_RP_TYPE_WHITE,
        plug_id == 0 ? &security_rp_sign_key_black_gfdmv4
                     : &security_rp_sign_key_white_eamuse,
        mcode,
        id,
        &data);
    memcpy(eeprom, &data, sizeof(data));

    return S_OK;
}

static const struct p3io_ops gfdm_p3io_ops = {
    .read_jamma = gfdm_read_jamma,
    .set_outputs = gfdm_set_outputs,
    .get_dipsw = gfdm_get_dipsw,
    .get_cab_type = gfdm_get_cab_type,
    .get_video_freq = gfdm_get_video_freq,
    .get_coinstock = gfdm_get_coinstock,
    .get_roundplug = gfdm_get_roundplug,
};

static void gfdm_init(void)
{
    struct cconfig *config;
    const char *cmdline;

    if (gfdm_initialized) {
        return;
    }
    gfdm_initialized = true;

    gfdm_open_log_file();
    log_to_writer(gfdm_log_writer, NULL);
    log_info("GFDM V4 hook initialization started");

    config = cconfig_init();
    gfdmhook1_config_init(config);
    if (!cconfig_hook_config_init(
            config,
            GFDMHOOK1_INFO_HEADER "\n" GFDMHOOK1_CMD_USAGE,
            CCONFIG_CMD_USAGE_OUT_DBG)) {
        cconfig_finit(config);
        ExitProcess(EXIT_FAILURE);
    }
    gfdmhook1_config_get(&gfdm_config, config);
    cconfig_finit(config);

    gfdm_keyboard = gfdm_config.keyboard;
    gfdm_mcode = gfdm_config.mcode;
    gfdm_pcbid = gfdm_config.pcbid;
    gfdm_eamid = gfdm_config.eamid;

    cmdline = GetCommandLineA();
    gfdm_is_gf = gfdm_command_has_switch(cmdline, 'g');
    log_info(
        "GFDM launch mode: %s (command line: %s)",
        gfdm_is_gf ? "GF" : "DM",
        cmdline);

    input_set_loggers(
        log_impl_misc, log_impl_info, log_impl_warning, log_impl_fatal);
    input_init(crt_thread_create, crt_thread_join, crt_thread_destroy);
    gfdm_mapper_loaded = mapper_config_load(gfdm_is_gf ? "gf" : "dm");
    log_info(
        "GFDM %s input mapping %s",
        gfdm_is_gf ? "GF" : "DM",
        gfdm_mapper_loaded ? "loaded" : "not configured; using keyboard fallback");

    adapter_hook_init();
    adapter_hook_override(gfdm_config.adapter.override_ip);

    /* Match the security status contract used by the known-good V4 hook. */
    gfdm_apply_device_hooks(NULL);
    gfdm_apply_extio_hooks(NULL);
    gfdm_apply_avs_hooks(NULL);
    gfdm_apply_movie_hooks(NULL);
    hook_table_apply(NULL, "kernel32.dll", gfdm_loader_syms,
                     lengthof(gfdm_loader_syms));
    iohook_push_handler(p3io_emu_dispatch_irp);
    p3io_setupapi_insert_hooks(NULL);
    p3io_emu_init(&gfdm_p3io_ops, NULL);
    gfdmhook1_network_init(&gfdm_config.server);

    log_info("GFDM V4 P3IO emulation is ready");
}

static void(__cdecl *real_boot_main)(HWND);
static void __cdecl gfdm_boot_main(HWND hwnd)
{
    gfdm_init();
    real_boot_main(hwnd);
}

static const struct hook_symbol boot_syms[] = {
    {
        .name = "?boot_main@@YAXPAUHWND__@@@Z",
        .patch = gfdm_boot_main,
        .link = (void **) &real_boot_main,
    },
};

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, void *ctx)
{
    (void) mod;
    (void) ctx;

    if (reason == DLL_PROCESS_ATTACH) {
        gfdm_module = mod;
        gfdm_open_log_file();
        log_to_writer(gfdm_log_writer, NULL);
        hook_table_apply(NULL, "boot.dll", boot_syms, lengthof(boot_syms));
    }

    return TRUE;
}
