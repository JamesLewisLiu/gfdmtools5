#include <string.h>

#include "cconfig/cconfig-util.h"
#include "gfdmhook1/config.h"
#include "security/id.h"
#include "util/log.h"

#define SERVER_KEY "eamuse.server"
#define PCBID_KEY "eamuse.pcbid"
#define EAMID_KEY "eamuse.eamid"
#define MCODE_KEY "security.mcode"
#define KEYBOARD_KEY "input.keyboard"

static const struct net_addr default_server = {
    .type = NET_ADDR_TYPE_HOSTNAME,
    .hostname = {.host = NET_LOCALHOST_NAME, .port = 80},
};

void gfdmhook1_config_init(struct cconfig *config)
{
    cconfig_util_set_str(
        config,
        SERVER_KEY,
        "localhost:80",
        "e-amusement server URL or host:port");
    cconfig_util_set_data(
        config,
        PCBID_KEY,
        (const uint8_t *) &security_id_default,
        sizeof(security_id_default),
        "black roundplug PCBID");
    cconfig_util_set_data(
        config,
        EAMID_KEY,
        (const uint8_t *) &security_id_default,
        sizeof(security_id_default),
        "white roundplug EAMID");
    cconfig_util_set_str(
        config,
        MCODE_KEY,
        "GCG32JAA",
        "V4 security mcode (8 characters)");
    cconfig_util_set_bool(
        config,
        KEYBOARD_KEY,
        true,
        "enable the default keyboard-to-panel mapping");
}

void gfdmhook1_config_get(
    struct gfdmhook1_config *out, struct cconfig *config)
{
    char server[1024];
    char mcode[sizeof(struct security_mcode) + 1];

    memset(out, 0, sizeof(*out));
    out->server = default_server;
    out->pcbid = security_id_default;
    out->eamid = security_id_default;
    out->keyboard = true;

    if (cconfig_util_get_str(config, SERVER_KEY, server, sizeof(server), "localhost:80")) {
        if (!net_str_parse(server, &out->server)) {
            log_warning("Invalid %s, using localhost:80", SERVER_KEY);
            out->server = default_server;
        }
    }

    cconfig_util_get_data(
        config, PCBID_KEY, (uint8_t *) &out->pcbid, sizeof(out->pcbid),
        (const uint8_t *) &security_id_default);
    cconfig_util_get_data(
        config, EAMID_KEY, (uint8_t *) &out->eamid, sizeof(out->eamid),
        (const uint8_t *) &security_id_default);

    if (!security_id_verify(&out->pcbid)) {
        out->pcbid = security_id_default;
    }
    if (!security_id_verify(&out->eamid)) {
        out->eamid = security_id_default;
    }

    if (!cconfig_util_get_str(
            config, MCODE_KEY, mcode, sizeof(mcode), "GCG32JAA") ||
        !security_mcode_parse(mcode, &out->mcode)) {
        log_warning("Invalid %s, using GCG32JAA", MCODE_KEY);
        security_mcode_parse("GCG32JAA", &out->mcode);
    }

    cconfig_util_get_bool(config, KEYBOARD_KEY, &out->keyboard, true);
}
