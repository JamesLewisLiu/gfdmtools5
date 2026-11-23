#ifndef GFDMHOOK1_EA3_BOOT_H
#define GFDMHOOK1_EA3_BOOT_H

#include "gfdmhook1/avs-config.h"
#include "util/log.h"

static int gfdm_ea3_boot_expanded(
    unsigned int version, struct property_node *config,
    int (__cdecl *boot)(struct property_node *), bool (*mount_mods)(void))
{
    struct gfdm_avs_config expanded = {0};
    int result;

    if (version > 8) return boot(config);
    /* V4-V8 default to a 4 KiB XRPC workspace. CORE's service list fits on
       the wire but exceeds that size when expanded into AVS property nodes.
       EA3 imports this configuration synchronously; retain larger values. */
    if (!gfdm_ea3_config_prepare(&expanded, config)) {
        log_fatal("Cannot prepare V%u EA3 XRPC buffer configuration",
                  version);
    }
    log_info("GFDM V%u EA3 XRPC workspace: minimum 65536 bytes",
             version);
    result = boot(expanded.root);
    gfdm_avs_config_fini(&expanded);
    /* Preserve V8's previous mount timing: this callback never mounted mods. */
    if (version <= 7 && !mount_mods()) {
        log_fatal("GFDM data_mods mount failed");
    }
    return result;
}

#endif
