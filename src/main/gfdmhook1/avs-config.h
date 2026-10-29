#ifndef GFDMHOOK1_AVS_CONFIG_H
#define GFDMHOOK1_AVS_CONFIG_H

#include <stdbool.h>

struct property;
struct property_node;

struct gfdm_avs_config {
    void *buffer;
    struct property *property;
    struct property_node *root;
};

/* V4's native boot uses a fixed 1512-byte property buffer. Keep the input
   intact and expand a deep copy before adding executable-relative paths. */
bool gfdm_avs_config_prepare(
    struct gfdm_avs_config *config, struct property_node *source,
    const char *nvram, const char *raw);
void gfdm_avs_config_fini(struct gfdm_avs_config *config);

#endif
