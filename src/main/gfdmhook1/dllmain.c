#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bemanitools/input.h"
#include "cconfig/cconfig-hook.h"
#include "gfdmhook1/config.h"
#include "gfdmhook1/network.h"
#include "hook/iohook.h"
#include "hooklib/adapter.h"
#include "hook/table.h"
#include "p3io/cmd.h"
#include "p3ioemu/devmgr.h"
#include "p3ioemu/emu.h"
#include "security/rp-sign-key.h"
#include "security/rp3.h"
#include "util/defs.h"
#include "util/log.h"
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
static struct security_mcode gfdm_mcode;
static struct security_id gfdm_pcbid;
static struct security_id gfdm_eamid;
static int(__cdecl *real_device_get_jamma_history)(
    void *history, int max_entries);

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
        .name = "?device_get_jamma_history@@YAHPAUT_JAMMA_HISTORY_INFO@@H@Z",
        .patch = gfdm_device_get_jamma_history,
        .link = (void **) &real_device_get_jamma_history,
    },
};

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
    uint32_t state;

    if (gfdm_mapper_loaded) {
        return (uint32_t) mapper_update();
    }

    gfdm_read_keys(&state);

    return state;
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

    if (result > 0 || history == NULL || max_entries <= 0) {
        return result;
    }

    state = gfdm_read_input_state();

    if ((state & ((1u << 0) | (1u << 1))) == 0) {
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
    gfdm_is_gf = strstr(cmdline, " -g") != NULL;

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
    hook_table_apply(NULL, "libdevice.dll", gfdm_secplug_syms,
                     lengthof(gfdm_secplug_syms));
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
        log_to_writer(log_writer_debug, NULL);
        hook_table_apply(NULL, "boot.dll", boot_syms, lengthof(boot_syms));
    }

    return TRUE;
}
