#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

#define GFDMHOOK1_INFO_HEADER \
    "gfdmhook1 for GFDM V4, build " __DATE__ " " __TIME__ \
    ", gitrev " STRINGIFY(GITREV)
#define GFDMHOOK1_CMD_USAGE \
    "Usage: inject.exe gfdmhook1.dll gdv4.exe -g|-d [options...]"

static struct gfdmhook1_config gfdm_config;
static bool gfdm_initialized;
static bool gfdm_keyboard;
static struct security_mcode gfdm_mcode;
static struct security_id gfdm_pcbid;
static struct security_id gfdm_eamid;

static void gfdm_read_keys(uint32_t *state)
{
    *state = 0;

    if (!gfdm_keyboard) {
        return;
    }

    /* P3IO input is active-high. These bindings cover the service controls,
       both starts and the common GF/DM play controls. */
    if (GetAsyncKeyState(VK_F1) & 0x8000) *state |= 1u << 6; /* service */
    if (GetAsyncKeyState(VK_F2) & 0x8000) *state |= 1u << 4; /* test */
    if (GetAsyncKeyState('5') & 0x8000) *state |= 1u << 5; /* coin */
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) *state |= 1u << 0;
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) *state |= 1u << 2;
    if (GetAsyncKeyState('Z') & 0x8000) *state |= 1u << 9;
    if (GetAsyncKeyState('X') & 0x8000) *state |= 1u << 10;
    if (GetAsyncKeyState('C') & 0x8000) *state |= 1u << 11;
    if (GetAsyncKeyState('A') & 0x8000) *state |= 1u << 13;
    if (GetAsyncKeyState('S') & 0x8000) *state |= 1u << 14;
    if (GetAsyncKeyState('D') & 0x8000) *state |= 1u << 15;
    if (GetAsyncKeyState('F') & 0x8000) *state |= 1u << 17;
    if (GetAsyncKeyState('G') & 0x8000) *state |= 1u << 18;
}

static HRESULT gfdm_read_jamma(void *ctx, uint32_t *state)
{
    (void) ctx;
    gfdm_read_keys(state);
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

    adapter_hook_init();
    adapter_hook_override(gfdm_config.adapter.override_ip);

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
