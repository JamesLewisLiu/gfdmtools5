#ifndef GFDMHOOK1_CONFIG_H
#define GFDMHOOK1_CONFIG_H

#include <stdbool.h>

#include "cconfig/cconfig.h"
#include "hooklib/config-adapter.h"
#include "security/id.h"
#include "security/mcode.h"
#include "util/net.h"

struct gfdmhook1_config {
    struct net_addr server;
    struct hooklib_config_adapter adapter;
    struct security_id pcbid;
    struct security_id eamid;
    struct security_mcode mcode;
    bool keyboard;
    float frame_rate_limit;
    int32_t forced_refresh_rate;
};

void gfdmhook1_config_init(struct cconfig *config);
void gfdmhook1_config_get(
    struct gfdmhook1_config *out, struct cconfig *config);

#endif
