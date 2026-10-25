#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bemanitools/input.h"
#include "bemanitools/eamio.h"
#include "cconfig/cconfig-hook.h"
#include "gfdmhook1/bios.h"
#include "gfdmhook1/bootstrap.h"
#include "gfdmhook1/coin.h"
#include "gfdmhook1/codec.h"
#include "gfdmhook1/config.h"
#include "gfdmhook1/debug.h"
#include "gfdmhook1/effect.h"
#include "gfdmhook1/network.h"
#include "gfdmhook1/profile.h"
#include "gfdmhook1/storage.h"
#include "geninput/input-config.h"
#include "hook/d3d9.h"
#include "hook/iohook.h"
#include "hooklib/adapter.h"
#include "hook/table.h"
#include "imports/avs.h"
#include "iidxhook-util/d3d9.h"
#include "p3io/cmd.h"
#include "p3ioemu/devmgr.h"
#include "p3ioemu/emu.h"
#include "security/rp-sign-key.h"
#include "security/rp3.h"
#include "util/defs.h"
#include "util/hex.h"
#include "util/log.h"
#include "util/str.h"
#include "util/time.h"
#include "util/thread.h"

#define GFDMHOOK1_INFO_HEADER \
    "gfdmhook1 for GFDM V4-V8, build " __DATE__ " " __TIME__ \
    ", gitrev " STRINGIFY(GITREV)
#define GFDMHOOK1_CMD_USAGE \
    "Usage: inject.exe gfdmhook1.dll gdv[4-8].exe -g|-d [options...]"

static struct gfdmhook1_config gfdm_config;
static bool gfdm_initialized;
static bool gfdm_keyboard;
static bool gfdm_mapper_loaded;
static bool gfdm_is_gf;
static FILE *gfdm_log_file;
static HMODULE gfdm_module;
static const struct gfdm_profile *gfdm_profile;
static bool gfdm_d3d9_initialized;
static uint64_t gfdm_gl_present_time;
static uint64_t gfdm_gl_present_count;
static DWORD gfdm_gl_present_log_time;
static bool gfdm_gl_present_logged;
static HMODULE gfdm_eamio_module;
static bool gfdm_eamio_initialized;

typedef void (*gfdm_eam_set_loggers_t)(
    log_formatter_t, log_formatter_t, log_formatter_t, log_formatter_t);
typedef bool (*gfdm_eam_init_t)(thread_create_t, thread_join_t, thread_destroy_t);
typedef uint16_t (*gfdm_eam_keypad_t)(uint8_t);
typedef uint8_t (*gfdm_eam_sensor_t)(uint8_t);
typedef uint8_t (*gfdm_eam_read_card_t)(uint8_t, uint8_t *, uint8_t);

static gfdm_eam_set_loggers_t gfdm_eam_set_loggers;
static gfdm_eam_init_t gfdm_eam_init;
static gfdm_eam_keypad_t gfdm_eam_get_keypad_state;
static gfdm_eam_sensor_t gfdm_eam_get_sensor_state;
static gfdm_eam_read_card_t gfdm_eam_read_card;
static bool gfdm_card_present[2];
static bool gfdm_card_sensor_seen[2];
static uint8_t gfdm_card_data[2][8];
static uint8_t gfdm_card_type[2];
static bool gfdm_card_read_logged[2];
static bool gfdm_card_read2_logged[2];
static int gfdm_card_eject_pending[2];
static uint16_t gfdm_keypad_seen[2];
static bool gfdm_card_status_logged[2];
static int gfdm_card_sensor_logged[2] = {-1, -1};
static uint32_t gfdm_last_output_state;
static bool gfdm_output_state_logged;
static uint32_t gfdm_last_device_input[2];
static bool gfdm_device_input_logged[2];
/* The native game consumes a persistent four-phase rotary position. */
static struct gfdm_effect_state gfdm_effectors[2];
static volatile LONG gfdm_input_frame;
static struct gfdm_coin_state gfdm_coins;
static struct gfdm_debug_state gfdm_debug;
static CRITICAL_SECTION gfdm_input_lock;
static bool gfdm_test_history_active;
static bool gfdm_service_history_active;
static struct security_mcode gfdm_mcode;
static struct security_id gfdm_pcbid;
static struct security_id gfdm_eamid;
static char gfdm_system_id[sizeof(struct security_id) * 2 + 1];
static char gfdm_bios_date[GFDM_BIOS_DATE_SIZE];
static int(__cdecl *real_std_setenv)(const char *key, const char *value);
static char(__cdecl *real_gd_dip_get_debug_dip_param)(int param);
static int(__cdecl *real_bmsd_boot)(
    HWND hwnd, void *work, unsigned int work_size, int mode,
    char port_mode, int port_count, int flags);
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
static void gfdm_apply_system_hooks(void);
static uint32_t gfdm_read_input_state(void);

static HRESULT gfdm_d3d9_irp_handler(struct hook_d3d9_irp *irp)
{
    /* The IIDX handler replaces GetClientRect with the D3D back-buffer size.
       V4 renders its main window with OpenGL, so that size is uninitialized
       until a D3D device is created. Drivers also query the client rectangle
       after focus changes; returning 0x0 then stops visible OpenGL updates.
       Preserve the real client rectangle while retaining the shared D3D
       limiter and device handling for movie playback. */
    if (irp->op == HOOK_D3D9_IRP_OP_GET_CLIENT_RECT) {
        return hook_d3d9_irp_invoke_next(irp);
    }

    return iidxhook_util_d3d9_irp_handler(irp);
}

static const hook_d3d9_irp_handler_t gfdm_d3d9_handlers[] = {
    gfdm_d3d9_irp_handler,
};

static BOOL(WINAPI *real_SwapBuffers)(HDC hdc);

static BOOL WINAPI gfdm_SwapBuffers(HDC hdc)
{
    uint64_t frame_time;

    InterlockedIncrement(&gfdm_input_frame);
    gfdm_gl_present_count++;
    if (!gfdm_gl_present_logged) {
        log_info("GFDM OpenGL SwapBuffers hook active");
        gfdm_gl_present_logged = true;
        gfdm_gl_present_log_time = GetTickCount();
    } else if (GetTickCount() - gfdm_gl_present_log_time >= 1000) {
        log_misc(
            "GFDM OpenGL SwapBuffers: %llu frame(s) in the last interval",
            (unsigned long long) gfdm_gl_present_count);
        gfdm_gl_present_count = 0;
        gfdm_gl_present_log_time = GetTickCount();
    }

    if (gfdm_config.frame_rate_limit > 0.0f) {
        frame_time = (uint64_t) (1000000.0 / gfdm_config.frame_rate_limit);
        if (gfdm_gl_present_time == 0) {
            gfdm_gl_present_time = time_get_counter();
        } else {
            uint64_t dt = time_get_elapsed_us(
                time_get_counter() - gfdm_gl_present_time);

            while (dt < frame_time) {
                if (frame_time - dt > 2000) {
                    Sleep(1);
                }
                dt = time_get_elapsed_us(
                    time_get_counter() - gfdm_gl_present_time);
            }
            gfdm_gl_present_time = time_get_counter();
        }
    }

    return real_SwapBuffers != NULL ? real_SwapBuffers(hdc) : FALSE;
}

static const struct hook_symbol gfdm_gdi_syms[] = {
    {
        .name = "SwapBuffers",
        .patch = gfdm_SwapBuffers,
        .link = (void **) &real_SwapBuffers,
    },
};

static void gfdm_setup_gfx_hooks(void)
{
    hook_table_apply(
        NULL, "gdi32.dll", gfdm_gdi_syms, lengthof(gfdm_gdi_syms));
}

static void gfdm_setup_d3d9_hooks(void)
{
    struct iidxhook_util_d3d9_config d3d9_config;

    if (gfdm_d3d9_initialized) {
        return;
    }

    gfdm_d3d9_initialized = true;
    iidxhook_util_d3d9_init_config(&d3d9_config);
    d3d9_config.framerate_limit = gfdm_config.frame_rate_limit;
    d3d9_config.forced_refresh_rate = gfdm_config.forced_refresh_rate;
    iidxhook_util_d3d9_configure(&d3d9_config);
    hook_d3d9_init(gfdm_d3d9_handlers, lengthof(gfdm_d3d9_handlers));
    log_info(
        "GFDM D3D9 limiter: %.3f FPS, forced refresh %d Hz",
        d3d9_config.framerate_limit,
        d3d9_config.forced_refresh_rate);
}

static void gfdm_setup_eamio(void)
{
    if (gfdm_eamio_initialized) {
        return;
    }

    gfdm_eamio_initialized = true;
    gfdm_eamio_module = GetModuleHandleA("eamio.dll");
    if (gfdm_eamio_module == NULL) {
        gfdm_eamio_module = LoadLibraryA("eamio.dll");
    }

    if (gfdm_eamio_module == NULL) {
        log_warning("GFDM card reader: eamio.dll was not found");
        return;
    }

    gfdm_eam_set_loggers = (gfdm_eam_set_loggers_t) GetProcAddress(
        gfdm_eamio_module, "eam_io_set_loggers");
    gfdm_eam_init = (gfdm_eam_init_t) GetProcAddress(
        gfdm_eamio_module, "eam_io_init");
    gfdm_eam_get_keypad_state = (gfdm_eam_keypad_t) GetProcAddress(
        gfdm_eamio_module, "eam_io_get_keypad_state");
    gfdm_eam_get_sensor_state = (gfdm_eam_sensor_t) GetProcAddress(
        gfdm_eamio_module, "eam_io_get_sensor_state");
    gfdm_eam_read_card = (gfdm_eam_read_card_t) GetProcAddress(
        gfdm_eamio_module, "eam_io_read_card");

    if (gfdm_eam_set_loggers == NULL || gfdm_eam_init == NULL ||
        gfdm_eam_get_keypad_state == NULL ||
        gfdm_eam_get_sensor_state == NULL || gfdm_eam_read_card == NULL) {
        log_warning("GFDM card reader: eamio.dll exports are incomplete");
        return;
    }

    gfdm_eam_set_loggers(
        log_impl_misc, log_impl_info, log_impl_warning, log_impl_fatal);
    if (!gfdm_eam_init(
            crt_thread_create, crt_thread_join, crt_thread_destroy)) {
        log_warning("GFDM card reader: eamio initialization failed");
        return;
    }

    log_info("GFDM card reader/keypad backend initialized");
}

static uint8_t gfdm_card_sensor_state(int unit_no)
{
    if (gfdm_eam_get_sensor_state == NULL || unit_no < 0 || unit_no >= 2) {
        return 0;
    }

    return gfdm_eam_get_sensor_state((uint8_t) unit_no);
}

/* V4's card-unit functions are polled from the game thread.  Consume the
   EAMIO sensor edge here and cache the eight-byte card ID for the subsequent
   cardunit_card_read() call.  game.dll does not start that read until
   cardunit_card_sensor() reports state 2. */
static void gfdm_cardunit_poll(void)
{
    int unit_no;
    uint8_t sensor;
    uint8_t result;

    if (gfdm_eam_get_sensor_state == NULL || gfdm_eam_read_card == NULL) {
        return;
    }

    for (unit_no = 0; unit_no < 2; unit_no++) {
        sensor = gfdm_card_sensor_state(unit_no);

        if (sensor != 0 && !gfdm_card_sensor_seen[unit_no]) {
            result = gfdm_eam_read_card(
                (uint8_t) unit_no, gfdm_card_data[unit_no], 8);
            if (result != EAM_IO_CARD_NONE) {
                gfdm_card_present[unit_no] = true;
                gfdm_card_type[unit_no] = result;
                log_info(
                    "GFDM card reader %d inserted (type %u)",
                    unit_no,
                    result);
            } else {
                gfdm_card_present[unit_no] = false;
                gfdm_card_type[unit_no] = EAM_IO_CARD_NONE;
                memset(gfdm_card_data[unit_no], 0, 8);
                log_misc("GFDM card reader %d sensor edge without card", unit_no);
            }
        }

        /* EAMIO's sensor is a short insert/tap pulse, not a persistent
           presence switch.  A low level only arms the next rising edge;
           card removal is driven by cardunit_card_eject(). */
        gfdm_card_sensor_seen[unit_no] = sensor != 0;
    }
}

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
                   "%sgfdm-v%u-hook.log", module_path, gfdm_profile->version);
        gfdm_log_file = fopen(log_path, "a");
    }

    if (gfdm_log_file == NULL) {
        str_format(log_path, sizeof(log_path), "gfdm-v%u-hook.log",
                   gfdm_profile->version);
        gfdm_log_file = fopen(log_path, "a");
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
    gfdm_cardunit_poll();
}

static void __cdecl gfdm_cardunit_card_eject(int unit_no)
{
    if (unit_no >= 0 && unit_no < 2) {
        gfdm_card_eject_pending[unit_no] = 1;
    }
}

static int __cdecl gfdm_cardunit_card_eject_wait(int unit_no)
{
    if (unit_no < 0 || unit_no >= 2) {
        return 0;
    }

    return gfdm_card_eject_pending[unit_no];
}

static int __cdecl gfdm_cardunit_get_errorcount(int unit_no)
{
    (void) unit_no;

    return 0;
}

static int __cdecl gfdm_cardunit_get_status(int unit_no)
{
    gfdm_cardunit_poll();
    if (unit_no >= 0 && unit_no < 2 && !gfdm_card_status_logged[unit_no]) {
        log_misc("GFDM cardunit %d status -> ready", unit_no);
        gfdm_card_status_logged[unit_no] = true;
    }

    return 1;
}

static int __cdecl gfdm_cardunit_card_sensor(int unit_no)
{
    int result;

    if (unit_no < 0 || unit_no >= 2) {
        return 0;
    }

    gfdm_cardunit_poll();
    result = gfdm_card_present[unit_no] ? 2 : 0;
    if (gfdm_card_sensor_logged[unit_no] != result) {
        log_misc("GFDM cardunit %d sensor -> %d", unit_no, result);
        gfdm_card_sensor_logged[unit_no] = result;
    }

    return result;
}

static int __cdecl gfdm_cardunit_card_sensor_raw(int unit_no)
{
    (void) unit_no;

    return 1;
}

static int __cdecl gfdm_cardunit_card_eject_complete(int unit_no)
{
    if (unit_no < 0 || unit_no >= 2) {
        return 0;
    }

    if (gfdm_card_eject_pending[unit_no]) {
        gfdm_card_present[unit_no] = false;
        gfdm_card_sensor_seen[unit_no] = false;
        gfdm_card_type[unit_no] = EAM_IO_CARD_NONE;
        memset(gfdm_card_data[unit_no], 0, 8);
    }

    return gfdm_card_eject_pending[unit_no];
}

static int __cdecl gfdm_cardunit_card_read(int unit_no, void *card)
{
    if (unit_no < 0 || unit_no >= 2 || card == NULL) {
        return 1;
    }

    gfdm_cardunit_poll();
    if (!gfdm_card_present[unit_no]) {
        memset(card, 0, 8);
        if (!gfdm_card_read_logged[unit_no]) {
            log_info("GFDM card reader %d read: no card", unit_no);
            gfdm_card_read_logged[unit_no] = true;
        }
        return 1;
    }

    memcpy(card, gfdm_card_data[unit_no], 8);
    if (!gfdm_card_read_logged[unit_no]) {
        log_info("GFDM card reader %d read: card present", unit_no);
        gfdm_card_read_logged[unit_no] = true;
    }

    return 0;
}

static int __cdecl gfdm_cardunit_card_read2(
    int unit_no, uint8_t *card, int *card_type)
{
    if (unit_no < 0 || unit_no >= 2 || card == NULL) {
        return 1;
    }

    gfdm_cardunit_poll();
    if (!gfdm_card_present[unit_no]) {
        if (card_type != NULL) {
            *card_type = EAM_IO_CARD_NONE;
        }
        memset(card, 0, 8);
        if (!gfdm_card_read2_logged[unit_no]) {
            log_info("GFDM card reader %d read2: no card", unit_no);
            gfdm_card_read2_logged[unit_no] = true;
        }
        return 1;
    }

    memcpy(card, gfdm_card_data[unit_no], 8);
    if (card_type != NULL) {
        *card_type = gfdm_card_type[unit_no];
    }
    if (!gfdm_card_read2_logged[unit_no]) {
        log_info(
            "GFDM card reader %d read2: card present type %u",
            unit_no,
            gfdm_card_type[unit_no]);
        gfdm_card_read2_logged[unit_no] = true;
    }

    return 0;
}

static int __cdecl gfdm_cardunit_card_cardnumber(
    const uint8_t *card, int unit_no, char *number, int nbytes)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    (void) unit_no;

    if (card == NULL || number == NULL || nbytes < 17) {
        return -1;
    }

    for (i = 0; i < 8; i++) {
        number[i * 2] = hex[card[i] >> 4];
        number[i * 2 + 1] = hex[card[i] & 0x0F];
    }
    number[16] = '\0';

    return 0;
}

static void __cdecl gfdm_cardunit_card_ready(int unit_no)
{
    if (unit_no >= 0 && unit_no < 2) {
        gfdm_card_eject_pending[unit_no] = 0;
    }
}

static int __cdecl gfdm_cardunit_key_get(int unit_no)
{
    uint16_t state;
    uint16_t pressed;
    unsigned int i;

    if (gfdm_eam_get_keypad_state == NULL || unit_no < 0 || unit_no >= 2) {
        return -1;
    }

    state = gfdm_eam_get_keypad_state((uint8_t) unit_no);
    pressed = state & (uint16_t) ~gfdm_keypad_seen[unit_no];
    gfdm_keypad_seen[unit_no] = state;
    for (i = 0; i < EAM_IO_KEYPAD_COUNT; i++) {
        if ((pressed & (1u << i)) != 0) {
            log_misc(
                "GFDM card keypad %d: scan/code %u",
                unit_no,
                i);
            return (int) i;
        }
    }

    return -1;
}

static const char *__cdecl gfdm_cardunit_key_str(int key_code)
{
    static const char *const names[] = {
        "0", "1", "4", "7", "00", "2", "5", "8", "", "3",
        "6", "9"};

    if (key_code < 0 || key_code >= (int) lengthof(names)) {
        return NULL;
    }

    return names[key_code];
}

static int __cdecl gfdm_cardunit_reset(void)
{
    memset(gfdm_card_present, 0, sizeof(gfdm_card_present));
    memset(gfdm_card_sensor_seen, 0, sizeof(gfdm_card_sensor_seen));
    memset(gfdm_card_data, 0, sizeof(gfdm_card_data));
    memset(gfdm_card_type, 0, sizeof(gfdm_card_type));
    memset(gfdm_card_eject_pending, 0, sizeof(gfdm_card_eject_pending));
    memset(gfdm_keypad_seen, 0, sizeof(gfdm_keypad_seen));
    gfdm_card_sensor_logged[0] = -1;
    gfdm_card_sensor_logged[1] = -1;
    return 0;
}

static void __cdecl gfdm_cardunit_shutdown(void)
{
    /* game.dll calls cardunit_shutdown() while changing between the normal
       game and TEST mode, then boots the card units again in the same
       process.  eam_io_fini() destroys eamio's state and joins its message
       thread, so calling it here either blocks that transition or leaves the
       next cardunit poll using a destroyed backend.  Keep eamio alive for
       the lifetime of gdv4.exe and only reset the V4-facing reader state. */
    log_misc("GFDM cardunit shutdown: preserving EAMIO backend");
    gfdm_cardunit_reset();
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
        .name = "?cardunit_card_read2@@YAHHQAEPEAH@Z",
        .patch = gfdm_cardunit_card_read2,
    },
    {
        .name = "?cardunit_card_cardnumber@@YAHQEBEHPEADH@Z",
        .patch = gfdm_cardunit_card_cardnumber,
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
    const char *nvram = gfdm_storage_nvram();
    const char *raw = gfdm_storage_raw();

    if (!gfdm_storage_prepare()) {
        log_fatal("Cannot prepare GFDM save directories beside the executable");
    }

    gfdm_replace_property_str(config, "/fs/nvram/device", nvram);
    gfdm_replace_property_str(config, "/fs/raw/device", raw);
    log_info("GFDM V%u AVS paths: nvram=%s raw=%s",
             gfdm_profile->version, nvram, raw);

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
        /* Every GFDM profile can load network consumers after boot_main.
           Re-apply the common adapter filter without changing its selected IP. */
        adapter_hook_init();
        gfdm_setup_d3d9_hooks();
        gfdm_setup_gfx_hooks();
        if (name != NULL && _stricmp(name, "d3d9.dll") == 0) {
            /* d3d9.dll is normally loaded after boot_main. Re-apply the
               table hooks now that its exports are present. */
            hook_d3d9_init(
                gfdm_d3d9_handlers, lengthof(gfdm_d3d9_handlers));
        }
        /* The V4 system/error libraries are loaded after boot_main. */
        gfdm_apply_device_hooks(module);
        gfdm_apply_extio_hooks(module);
        gfdm_apply_avs_hooks(NULL);
        /* The import we need to patch lives in game.dll, not libmovie.dll.
           Re-scan all loaded modules after late movie loading so the game's
           IAT is updated even when game.dll was loaded before libmovie. */
        gfdm_apply_movie_hooks(NULL);
        gfdm_codec_apply_hooks();
        gfdm_apply_system_hooks();
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
static void __cdecl gfdm_device_get_coinstock(
    uint16_t *slot1, uint16_t *slot2);
static void __cdecl gfdm_device_get_coinstock_all(
    uint16_t *slot1, uint16_t *slot2);

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
};

static const struct hook_symbol gfdm_input_syms[] = {
    {
        .name = "?device_get_input@@YAIH@Z",
        .patch = gfdm_device_get_input,
        .link = (void **) &real_device_get_input,
    },
    {
        /* V4's 32-bit build uses the Pxxxx (unsigned short *) decoration.
           libshare-pj polls this function for the normal credit path. */
        .name = "?device_get_coinstock@@YAXPAG0@Z",
        .patch = gfdm_device_get_coinstock,
    },
    {
        .name = "?device_get_coinstock_all@@YAXPAG0@Z",
        .patch = gfdm_device_get_coinstock_all,
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

static int __cdecl gfdm_std_setenv(const char *key, const char *value)
{
    /* Like launcher/ea3-config.c, use the configured PCBID as system identity.
       V4 otherwise encodes the white ROM with a fixed 0140 prefix and byte
       permutation. Override at the environment producer so libsystem,
       libshare-pj and boot's eam_init all consume the same complete PCBID.
       Keep the physical white ROM and its EEPROM signature tied to EAMID. */
    if (key != NULL && strcmp(key, "/env/profile/sysid") == 0) {
        log_info("GFDM system/EA3 PCBID: %s", gfdm_system_id);
        value = gfdm_system_id;
    }

    return real_std_setenv(key, value);
}

static const struct hook_symbol gfdm_avs_syms[] = {
    {
        .name = "avs_boot",
        .patch = gfdm_avs_boot,
        .link = (void **) &real_avs_boot,
    },
    {
        .name = "std_setenv",
        .patch = gfdm_std_setenv,
        .link = (void **) &real_std_setenv,
    },
};

static char *__cdecl gfdm_sys_code_get_bios_date(void)
{
    return gfdm_bios_date;
}

static char __cdecl gfdm_gd_dip_get_debug_dip_param(int param)
{
    static bool logged[2];
    static char last_value[2];
    char native_value;
    char value;
    int index;

    native_value = real_gd_dip_get_debug_dip_param(param);
    if (param != 1 && param != 2) {
        return native_value;
    }

    gfdm_read_input_state();
    EnterCriticalSection(&gfdm_input_lock);
    value = gfdm_debug_param(&gfdm_debug, param, native_value);
    index = param - 1;
    if (!logged[index] || last_value[index] != value) {
        log_info("GFDM debug DIP %s -> %u",
                 param == 2 ? "autoplay" : "neverend", (unsigned char) value);
        last_value[index] = value;
        logged[index] = true;
    }
    LeaveCriticalSection(&gfdm_input_lock);
    return value;
}

static const struct hook_symbol gfdm_debug_syms[] = {
    {
        .name = "gd_dip_get_debug_dip_param",
        .patch = gfdm_gd_dip_get_debug_dip_param,
        .link = (void **) &real_gd_dip_get_debug_dip_param,
    },
};

static const struct hook_symbol gfdm_system_syms[] = {
    {
        .name = "sys_code_get_bios_date",
        .patch = gfdm_sys_code_get_bios_date,
    },
};

static int __cdecl gfdm_bmsd_boot(
    HWND hwnd, void *work, unsigned int work_size, int mode,
    char port_mode, int port_count, int flags)
{
    int result;

    /* V4-V8 flag 1 skips the obsolete WinMM system master-volume setup.
       Digital/HDMI endpoints need not expose its DST_SPEAKERS mixer line;
       failure there aborts bmsd_boot before DirectSound is even created.
       Keep the native renderer and game volumes, and leave Windows volume
       to the user. V5-V8 flag 2 also changes rendering, so preserve it. */
    if (flags == 0) {
        flags = 1;
        log_info("GFDM audio: bypassing legacy Windows master-volume setup");
    }

    result = real_bmsd_boot(
        hwnd, work, work_size, mode, port_mode, port_count, flags);
    if (result < 0) {
        log_warning("GFDM audio: native bmsd_boot failed (%d)", result);
    } else {
        log_info("GFDM audio: native bmsd_boot succeeded (%d)", result);
    }

    return result;
}

static const struct hook_symbol gfdm_audio_syms[] = {
    {
        .name = "bmsd_boot",
        .patch = gfdm_bmsd_boot,
        .link = (void **) &real_bmsd_boot,
    },
};

static void gfdm_apply_system_hooks(void)
{
    /* game.dll may be loaded after libsystem.dll, so scan all consumers. */
    hook_table_apply(NULL, "libsystem.dll", gfdm_system_syms,
                     lengthof(gfdm_system_syms));
    hook_table_apply(NULL, "libshare-pj.dll", gfdm_debug_syms,
                     lengthof(gfdm_debug_syms));
    hook_table_apply(NULL, "libbmsd.dll", gfdm_audio_syms,
                     lengthof(gfdm_audio_syms));
}

static void gfdm_apply_device_hooks(HMODULE target)
{
    (void) target;
    /* The consumer is game.dll/libshare-pj.dll, not libdevice.dll itself.
       When those DLLs are loaded after boot_main, applying only to the newly
       loaded module misses their import tables.  Rescan all modules just as
       the movie hook does. */
    if (gfdm_profile->version == 4) {
        hook_table_apply(NULL, "libdevice.dll", gfdm_secplug_syms,
                         lengthof(gfdm_secplug_syms));
        hook_table_apply(NULL, "device.dll", gfdm_secplug_syms,
                         lengthof(gfdm_secplug_syms));
    }
    /* V5-V8 use UDONHRKI/E-AMUSE3, matching our signed P3IO payloads.
       Leave their native security checks and output buffers intact. */
    hook_table_apply(NULL, "libdevice.dll", gfdm_input_syms,
                     lengthof(gfdm_input_syms));
    hook_table_apply(NULL, "device.dll", gfdm_input_syms,
                     lengthof(gfdm_input_syms));
}

static void gfdm_apply_extio_hooks(HMODULE target)
{
    (void) target;
    hook_table_apply(NULL, "libextio.dll", gfdm_extio_syms,
                     lengthof(gfdm_extio_syms));
}

static void gfdm_apply_avs_hooks(HMODULE target)
{
    (void) target;
    /* A newly loaded game module can bring libdevice in as a dependency. */
    hook_table_apply(NULL, "libavs-win32.dll", gfdm_avs_syms,
                     lengthof(gfdm_avs_syms));
}

static void gfdm_apply_movie_hooks(HMODULE target)
{
    /* The decoder RVA and object layout above were verified only for V4. */
    if (!gfdm_profile->movie_v4_guard) {
        return;
    }
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
    if (GetAsyncKeyState(VK_F3) & 0x8000) *state |= GFDM_DEBUG_AUTO;
    if (GetAsyncKeyState(VK_F4) & 0x8000) *state |= GFDM_DEBUG_TIMER;

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
        if (GetAsyncKeyState('E') & 0x8000) *state |= GFDM_EFFECT_REVERSE_P1;
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

static void gfdm_update_effector_axis(unsigned int player)
{
    struct mapped_analog ma;
    bool bound;

    bound = mapper_get_analog_map((uint8_t) player, &ma) && ma.hid != NULL;
    gfdm_effect_axis(&gfdm_effectors[player], bound,
        bound && mapper_is_analog_absolute((uint8_t) player),
        bound ? mapper_read_analog((uint8_t) player) : 0);
}

static uint32_t gfdm_get_effector_input(unsigned int player)
{
    uint32_t result;
    uint8_t before;

    EnterCriticalSection(&gfdm_input_lock);
    before = gfdm_effectors[player].phase;
    result = gfdm_effect_publish(&gfdm_effectors[player],
        (uint32_t) InterlockedCompareExchange(&gfdm_input_frame, 0, 0));
    if (before != gfdm_effectors[player].phase) {
        log_misc("GFDM effector P%u phase: %u -> %u",
            player + 1, before, gfdm_effectors[player].phase);
    }
    LeaveCriticalSection(&gfdm_input_lock);
    return result;
}

static uint32_t gfdm_read_input_state(void)
{
    static bool first_update = true;
    static uint32_t last_state;
    static uint8_t last_analog[2];
    uint32_t state;
    uint32_t keyboard_state;
    uint32_t debug_changed;
    uint8_t analog;
    unsigned int analog_no;

    /* Serialize sampling with edge updates: a stale sample from another
       caller must not look like a release followed by a second press. */
    EnterCriticalSection(&gfdm_input_lock);
    state = gfdm_mapper_loaded ? (uint32_t) mapper_update() : 0;

    if (gfdm_mapper_loaded && gfdm_is_gf) {
        for (analog_no = 0; analog_no < 2; analog_no++) {
            analog = mapper_read_analog((uint8_t) analog_no);
            if (first_update || analog != last_analog[analog_no]) {
                log_misc(
                    "GFDM effector analog %u: %u",
                    analog_no,
                    analog);
                last_analog[analog_no] = analog;
            }

            gfdm_update_effector_axis(analog_no);
        }
    }

    /* The keyboard mapping is an explicit fallback.  In particular, do not
       merge it into the mapper state when input.keyboard=false: doing so made
       it impossible to tell whether a configured HID action was reaching the
       game. */
    if (gfdm_keyboard) {
        gfdm_read_keys(&keyboard_state);
        state |= keyboard_state;
    }

    if (gfdm_is_gf) {
        gfdm_effect_buttons(&gfdm_effectors[0],
            (state & (1u << 28)) != 0,
            (state & GFDM_EFFECT_REVERSE_P1) != 0);
        gfdm_effect_buttons(&gfdm_effectors[1],
            (state & (1u << 29)) != 0,
            (state & GFDM_EFFECT_REVERSE_P2) != 0);
    }

    /* Count only physical press edges. Keep the cumulative board counter
       independent of the game-facing consumption cursor. */
    if (gfdm_coin_update(&gfdm_coins, (state & (1u << 2)) != 0)) {
        log_info("GFDM coin inserted, total=%u", gfdm_coins.total);
    }
    debug_changed = gfdm_debug_update(&gfdm_debug, state);
    if (debug_changed & GFDM_DEBUG_AUTO) {
        log_info("GFDM debug switch Auto Play: %s",
                 gfdm_debug.enabled & GFDM_DEBUG_AUTO ? "ON" : "OFF");
    }
    if (debug_changed & GFDM_DEBUG_TIMER) {
        log_info("GFDM debug switch Disable Menu Timer: %s",
                 gfdm_debug.enabled & GFDM_DEBUG_TIMER ? "ON" : "OFF");
    }
    if (first_update || state != last_state) {
        log_misc(
            "GFDM input state: %08x (%s mapper)",
            state,
            gfdm_mapper_loaded ? "configured" : "keyboard");
        last_state = state;
        first_update = false;
    }

    LeaveCriticalSection(&gfdm_input_lock);
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
    if (state & (1u << 2)) result |= 0x01; /* COIN */

    if (gfdm_is_gf) {
        if (player != 1) {
            if (state & (1u << 8)) result |= 0x0004;  /* START */
            if (state & (1u << 12)) result |= 0x0040; /* WAIL (DOWN) */
            if (state & (1u << 18)) result |= 0x0200; /* RED (BUTTON0) */
            if (state & (1u << 20)) result |= 0x0400; /* GREEN (BUTTON1) */
            if (state & (1u << 22)) result |= 0x0800; /* BLUE (BUTTON2) */
            if (state & (1u << 24)) result |= 0x0020; /* PICK A (UP) */
            if (state & (1u << 25)) result |= 0x0040; /* PICK B (DOWN) */
            result |= gfdm_get_effector_input(0);
        } else {
            if (state & (1u << 9)) result |= 0x0004;
            if (state & (1u << 13)) result |= 0x0040;
            if (state & (1u << 19)) result |= 0x0200;
            if (state & (1u << 21)) result |= 0x0400;
            if (state & (1u << 23)) result |= 0x0800;
            if (state & (1u << 26)) result |= 0x0020;
            if (state & (1u << 27)) result |= 0x0040;
            result |= gfdm_get_effector_input(1);
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

    /* Mapper debug actions control software DIPs. The compact 0x20000 and
       0x40000 bits are debug-menu navigation, not Auto Play / Neverend. */

    if (player >= 0 && player < 2 &&
        (!gfdm_device_input_logged[player] ||
         gfdm_last_device_input[player] != result)) {
        log_misc("GFDM device input player %d: %08x", player, result);
        gfdm_last_device_input[player] = result;
        gfdm_device_input_logged[player] = true;
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
       pressed so the screen cannot discard the mapped key as stale input.
       The API is an edge queue, not a level query: return the synthetic entry
       once and let subsequent polls observe an empty queue until release. */
    if ((state & (1u << 1)) != 0) {
        if (!gfdm_test_history_active) {
            log_misc("Synthesized GFDM JAMMA history for TEST");
            gfdm_test_history_active = true;

            entry = (uint8_t *) history;
            memset(entry, 0, 12);
            tick = GetTickCount();
            memcpy(entry, &tick, sizeof(tick));
            memcpy(entry + 4, &tick, sizeof(tick));
            entry[8] = 0x20;

            return 1;
        }
        gfdm_service_history_active = false;

        return result;
    }

    gfdm_test_history_active = false;
    if ((state & (1u << 0)) == 0) {
        gfdm_service_history_active = false;
    }

    if (result > 0 || (state & (1u << 0)) == 0) {
        return result;
    }

    if (!gfdm_service_history_active) {
        entry = (uint8_t *) history;
        memset(entry, 0, 12);
        tick = GetTickCount();
        memcpy(entry, &tick, sizeof(tick));
        memcpy(entry + 4, &tick, sizeof(tick));

        /* History bit 6 is the V4 SERVICE edge. */
        *(uint16_t *) (entry + 8) = 0x40;
        log_misc("Synthesized GFDM JAMMA history for SERVICE");
        gfdm_service_history_active = true;

        return 1;
    }

    return result;
}

static HRESULT gfdm_read_jamma(void *ctx, uint32_t *state)
{
    uint32_t input;

    (void) ctx;

    /* libdevice's asynchronous reader expects the hardware interrupt read
       to block. An immediately completed emulated IOCTL makes it spin at
       tens of thousands of reads per second, holding the input lock and
       starving the game thread. Yield before sampling, outside that lock. */
    Sleep(1);
    input = gfdm_read_input_state();

    /* P3IO's JAMMA system inputs are not the compact libdevice bits:
       bit 4 = TEST, bit 5 = COIN, bit 6 = SERVICE.  Keep the game button
       bits untouched and translate only the three system inputs. */
    *state = input & ~(UINT32_C(0x07) | GFDM_DEBUG_MASK
        | (gfdm_is_gf ? GFDM_EFFECT_REVERSE_MASK : 0));
    if (input & (1u << 0)) *state |= 1u << 6;
    if (input & (1u << 1)) *state |= 1u << 4;
    if (input & (1u << 2)) *state |= 1u << 5;

    static uint32_t last_jamma;
    static bool jamma_logged;
    if (!jamma_logged || last_jamma != *state) {
        log_misc("GFDM JAMMA input: %08x", *state);
        last_jamma = *state;
        jamma_logged = true;
    }

    return S_OK;
}

static HRESULT gfdm_set_outputs(void *ctx, uint32_t state)
{
    (void) ctx;
    if (!gfdm_output_state_logged || gfdm_last_output_state != state) {
        log_misc("P3IO outputs: %08x", state);
        gfdm_last_output_state = state;
        gfdm_output_state_logged = true;
    }
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
    EnterCriticalSection(&gfdm_input_lock);
    for (size_t i = 0; i < nslots; i++) {
        slots[i] = i == 0 ? gfdm_coins.total : 0;
    }
    LeaveCriticalSection(&gfdm_input_lock);
    return S_OK;
}

static void gfdm_take_coinstock(
    uint16_t *slot1, uint16_t *slot2, bool all)
{
    uint16_t count = 0;

    /* Native get_coinstock consumes at most one per slot; _all drains all
       pending events. Returning the board total here repeatedly adds credit
       after a single press, even when the key has already been released. */
    EnterCriticalSection(&gfdm_input_lock);
    if (slot1 != NULL) {
        count = gfdm_coin_take(&gfdm_coins, all);
        *slot1 = count;
    }
    if (slot2 != NULL) {
        *slot2 = 0;
    }
    LeaveCriticalSection(&gfdm_input_lock);
    if (count != 0) {
        log_info("GFDM coin consumed%s: %u", all ? " (all)" : "", count);
    }
}

static void __cdecl gfdm_device_get_coinstock(
    uint16_t *slot1, uint16_t *slot2)
{
    gfdm_take_coinstock(slot1, slot2, false);
}

static void __cdecl gfdm_device_get_coinstock_all(
    uint16_t *slot1, uint16_t *slot2)
{
    gfdm_take_coinstock(slot1, slot2, true);
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

    /* Both ROMs carry their configured ID, which is also used for the
       EEPROM signature. V4 identifies a white plug by the decoded EEPROM
       mcode (eight '@' bytes), then derives SYSTEM ID from its ROM. */
    memcpy(rom, id->id, sizeof(id->id));
    security_rp3_generate_signed_eeprom_data(
        plug_id == 0 ? SECURITY_RP_UTIL_RP_TYPE_BLACK
                     : SECURITY_RP_UTIL_RP_TYPE_WHITE,
        plug_id == 0 ? gfdm_profile->black_sign_key
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
    const char *default_mcode;

    if (gfdm_initialized) {
        return;
    }
    gfdm_initialized = true;

    gfdm_open_log_file();
    log_to_writer(gfdm_log_writer, NULL);
    log_info("GFDM V%u hook initialization started", gfdm_profile->version);
    InitializeCriticalSection(&gfdm_input_lock);

    cmdline = GetCommandLineA();
    gfdm_is_gf = gfdm_command_has_switch(cmdline, 'g');
    if (!gfdm_storage_init(GetModuleHandleA("boot.dll"), gfdm_is_gf) ||
        !gfdm_storage_prepare_root()) {
        log_fatal("Cannot resolve GFDM save paths beside the executable");
    }

    default_mcode = gfdm_is_gf ? gfdm_profile->mcode_gf : gfdm_profile->mcode_dm;

    config = cconfig_init();
    gfdmhook1_config_init(config, default_mcode);
    if (!cconfig_hook_config_init(
            config,
            GFDMHOOK1_INFO_HEADER "\n" GFDMHOOK1_CMD_USAGE,
            CCONFIG_CMD_USAGE_OUT_DBG)) {
        cconfig_finit(config);
        ExitProcess(EXIT_FAILURE);
    }
    gfdmhook1_config_get(&gfdm_config, config, default_mcode);
    cconfig_finit(config);

    gfdm_keyboard = gfdm_config.keyboard;
    gfdm_mcode = gfdm_config.mcode;
    gfdm_pcbid = gfdm_config.pcbid;
    gfdm_eamid = gfdm_config.eamid;
    hex_encode_uc(&gfdm_pcbid, sizeof(gfdm_pcbid),
                  gfdm_system_id, sizeof(gfdm_system_id));
    gfdm_bios_read_date(gfdm_bios_date);
    gfdm_codec_init();
    log_info("GFDM BIOS date: %s", gfdm_bios_date);
    gfdm_setup_d3d9_hooks();
    gfdm_setup_gfx_hooks();

    log_info(
        "GFDM launch mode: %s (command line: %s)",
        gfdm_is_gf ? "GF" : "DM",
        cmdline);

    input_set_loggers(
        log_impl_misc, log_impl_info, log_impl_warning, log_impl_fatal);
    input_init(crt_thread_create, crt_thread_join, crt_thread_destroy);
    gfdm_setup_eamio();
    gfdm_mapper_loaded = mapper_config_load(gfdm_is_gf ? "gf" : "dm");
    log_info(
        "GFDM %s input mapping %s",
        gfdm_is_gf ? "GF" : "DM",
        gfdm_mapper_loaded ? "loaded" : "not configured; using keyboard fallback");

    adapter_hook_init();
    adapter_hook_override(gfdm_config.adapter.override_ip);
    log_info("GFDM V%u network adapter: %s", gfdm_profile->version,
             gfdm_config.adapter.override_ip[0] != '\0'
                 ? gfdm_config.adapter.override_ip : "default route");

    /* Match the security status contract used by the known-good V4 hook. */
    gfdm_apply_device_hooks(NULL);
    gfdm_apply_extio_hooks(NULL);
    gfdm_apply_avs_hooks(NULL);
    gfdm_apply_movie_hooks(NULL);
    gfdm_apply_system_hooks();
    hook_table_apply(NULL, "kernel32.dll", gfdm_loader_syms,
                     lengthof(gfdm_loader_syms));
    iohook_push_handler(p3io_emu_dispatch_irp);
    p3io_setupapi_insert_hooks(NULL);
    p3io_emu_init(&gfdm_p3io_ops, NULL);
    gfdmhook1_network_init(&gfdm_config.server);

    log_info("GFDM V%u P3IO emulation is ready", gfdm_profile->version);
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
        char executable[MAX_PATH];
        DWORD size;

        size = GetModuleFileNameA(NULL, executable, sizeof(executable));
        if (size == 0 || size >= sizeof(executable)) {
            return FALSE;
        }
        gfdm_profile = gfdm_profile_find(executable);
        if (gfdm_profile == NULL) {
            OutputDebugStringA("gfdmhook1: expected gdv4.exe through gdv8.exe\n");
            return FALSE;
        }
        gfdm_module = mod;
        gfdm_open_log_file();
        log_to_writer(gfdm_log_writer, NULL);
        if (!gfdm_bootstrap_init(gfdm_profile,
                gfdm_command_has_switch(GetCommandLineA(), 'g'))) {
            log_warning("Could not initialize built-in GFDM startup configuration");
            return FALSE;
        }
        hook_table_apply(NULL, "boot.dll", boot_syms, lengthof(boot_syms));
    }

    return TRUE;
}
