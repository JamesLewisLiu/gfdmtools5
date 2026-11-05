#include <stdlib.h>
#include <string.h>

#include "imports/avs.h"
#include "gfdmhook1/avs-config.h"

#define GFDM_AVS_CONFIG_CAPACITY (64 * 1024)

static bool gfdm_avs_config_replace(
    struct property_node *root, const char *path, const char *value)
{
    struct property_node *node = property_search(NULL, root, path);
    const char *actual;

    if (node != NULL) {
        property_node_remove(node);
    }
    node = property_node_create(NULL, root, PROPERTY_TYPE_STR, path, value);
    if (node == NULL || property_node_type(node) != PROPERTY_TYPE_STR) {
        return false;
    }
    node = property_search(NULL, root, path);
    if (node == NULL) {
        return false;
    }
    actual = property_node_refdata(node);
    return actual != NULL && strcmp(actual, value) == 0;
}

bool gfdm_avs_config_prepare(
    struct gfdm_avs_config *config, struct property_node *source,
    const char *nvram, const char *raw)
{
    memset(config, 0, sizeof(*config));
    if (source == NULL || nvram == NULL || raw == NULL) {
        return false;
    }
    config->buffer = calloc(1, GFDM_AVS_CONFIG_CAPACITY);
    if (config->buffer == NULL) {
        return false;
    }
    config->property = property_create(
        PROPERTY_FLAG_READ | PROPERTY_FLAG_WRITE | PROPERTY_FLAG_CREATE,
        config->buffer, GFDM_AVS_CONFIG_CAPACITY);
    if (config->property != NULL) {
        config->root = property_node_clone(config->property, NULL, source, true);
    }
    if (config->root == NULL ||
        !gfdm_avs_config_replace(config->root, "/fs/nvram/device", nvram) ||
        !gfdm_avs_config_replace(config->root, "/fs/raw/device", raw)) {
        gfdm_avs_config_fini(config);
        return false;
    }
    return true;
}

void gfdm_avs_config_fini(struct gfdm_avs_config *config)
{
    if (config->property != NULL) {
        property_destroy(config->property);
    }
    free(config->buffer);
    memset(config, 0, sizeof(*config));
}

bool gfdm_ea3_config_prepare(
    struct gfdm_avs_config *config, struct property_node *source)
{
    const char *path = "/network/sz_xrpc_buf";
    struct property_node *node;
    uint32_t size = GFDM_AVS_CONFIG_CAPACITY;
    uint32_t existing = 0;

    memset(config, 0, sizeof(*config));
    if (source == NULL) return false;
    config->buffer = calloc(1, GFDM_AVS_CONFIG_CAPACITY);
    if (config->buffer == NULL) return false;
    config->property = property_create(
        PROPERTY_FLAG_READ | PROPERTY_FLAG_WRITE | PROPERTY_FLAG_CREATE,
        config->buffer, GFDM_AVS_CONFIG_CAPACITY);
    if (config->property != NULL) {
        config->root = property_node_clone(config->property, NULL, source, true);
    }
    if (config->root == NULL) goto fail;
    node = property_search(NULL, config->root, path);
    if (node != NULL) {
        if (property_node_read(node, PROPERTY_TYPE_U32, &existing, sizeof(existing)) < 0)
            goto fail;
        if (existing >= size) return true;
        property_node_remove(node);
    }
    node = property_node_create(NULL, config->root, PROPERTY_TYPE_U32, path, size);
    if (node == NULL || property_node_read(
            node, PROPERTY_TYPE_U32, &existing, sizeof(existing)) < 0 || existing != size)
        goto fail;
    return true;
fail:
    gfdm_avs_config_fini(config);
    return false;
}
