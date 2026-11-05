#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imports/avs.h"

struct property { int marker; };
struct property_node {
    enum property_type type;
    char value[300];
    bool present;
};

static struct property arena;
static struct property_node original, copy, devices[3];
static int failure, allocated, freed, destroyed, creates;
static bool ea3_mode;
static uint32_t original_xrpc;

static void *fixture_calloc(size_t count, size_t size)
{
    if (failure == 7) return NULL;
    allocated++;
    return calloc(count, size);
}

static void fixture_free(void *buffer)
{
    if (buffer != NULL) freed++;
    free(buffer);
}

#define calloc fixture_calloc
#define free fixture_free
#include "gfdmhook1/avs-config.c"
#undef calloc
#undef free

struct property *property_create(int flags, void *buffer, uint32_t size)
{
    assert(flags == 7 && buffer != NULL && size == 65536);
    return failure == 1 ? NULL : &arena;
}

struct property_node *property_node_clone(
    struct property *prop, struct property_node *parent,
    struct property_node *source, bool deep)
{
    assert(prop == &arena && parent == NULL && source == &original && deep);
    if (failure == 2) return NULL;
    copy = original;
    for (int i = 0; i < 2; i++) {
        devices[i].present = true;
        devices[i].type = PROPERTY_TYPE_STR;
        strcpy(devices[i].value, "original short device");
    }
    devices[2].present = original_xrpc != 0;
    devices[2].type = PROPERTY_TYPE_U32;
    memcpy(devices[2].value, &original_xrpc, sizeof(original_xrpc));
    return &copy;
}

static int device_index(const char *path)
{
    if (strcmp(path, "/fs/nvram/device") == 0) return 0;
    if (strcmp(path, "/network/sz_xrpc_buf") == 0) return 2;
    assert(strcmp(path, "/fs/raw/device") == 0);
    return 1;
}

struct property_node *property_search(
    struct property *prop, struct property_node *root, const char *path)
{
    assert(prop == NULL && root == &copy);
    int i = device_index(path);
    return devices[i].present ? &devices[i] : NULL;
}

void property_node_remove(struct property_node *node)
{
    assert(node == &devices[0] || node == &devices[1] || node == &devices[2]);
    assert(node->present);
    node->present = false;
}

struct property_node *property_node_create(
    struct property *prop, struct property_node *root, int type,
    const char *path, ...)
{
    assert(prop == NULL && root == &copy);
    int i = device_index(path);
    assert(type == (i == 2 ? PROPERTY_TYPE_U32 : PROPERTY_TYPE_STR));
    assert(!devices[i].present);
    creates++;
    if (failure == 9 && i == 2) return NULL;
    if ((failure == 3 && i == 0) || (failure == 4 && i == 1)) return NULL;
    va_list args;
    va_start(args, path);
    if (i == 2) {
        uint32_t value = va_arg(args, uint32_t);
        memcpy(devices[i].value, &value, sizeof(value));
        va_end(args);
        devices[i].present = true;
        devices[i].type = PROPERTY_TYPE_U32;
        return &devices[i];
    }
    const char *value = va_arg(args, const char *);
    assert(strlen(value) < sizeof(devices[i].value));
    strcpy(devices[i].value, value);
    va_end(args);
    devices[i].present = true;
    devices[i].type = failure == 6 ? PROPERTY_TYPE_VOID : PROPERTY_TYPE_STR;
    return &devices[i];
}

int property_node_read(struct property_node *node, enum property_type type, void *data, uint32_t size)
{
    assert(ea3_mode && node == &devices[2] && type == PROPERTY_TYPE_U32 && size == 4);
    memcpy(data, node->value, 4);
    if (failure == 10 && creates != 0) *(uint32_t *) data = 0;
    return failure == 8 ? -1 : 4;
}

enum property_type property_node_type(struct property_node *node)
{
    return node->type;
}

const char *property_node_refdata(struct property_node *node)
{
    return failure == 5 ? NULL : node->value;
}

void property_destroy(struct property *prop)
{
    assert(prop == &arena);
    destroyed++;
}

int main(void)
{
    const char *nvram = "I:/a long isolated V4 directory/bin-diagnostic/DEV/GF/CONF/NVRAM";
    const char *raw = "I:/a long isolated V4 directory/bin-diagnostic/DEV/GF/CONF/RAW";
    struct gfdm_avs_config config;
    strcpy(original.value, "unrelated original config data");
    for (failure = 0; failure <= 7; failure++) {
        allocated = freed = destroyed = creates = 0;
        bool ok = gfdm_avs_config_prepare(&config, &original, nvram, raw);
        assert(ok == (failure == 0));
        assert(strcmp(original.value, "unrelated original config data") == 0);
        if (ok) {
            assert(config.root == &copy && config.root != &original);
            assert(strcmp(copy.value, original.value) == 0);
            assert(strcmp(devices[0].value, nvram) == 0);
            assert(strcmp(devices[1].value, raw) == 0 && creates == 2);
            assert(destroyed == 0 && freed == 0);
        }
        gfdm_avs_config_fini(&config);
        assert(config.root == NULL && config.property == NULL && config.buffer == NULL);
        assert(freed == allocated);
        assert(destroyed == (failure == 1 || failure == 7 ? 0 : 1));
        gfdm_avs_config_fini(&config);
        assert(freed == allocated);
    }
    failure = 0;
    allocated = 0;
    assert(!gfdm_avs_config_prepare(&config, NULL, nvram, raw));
    assert(allocated == 0);
    ea3_mode = true;
    const uint32_t sizes[] = {0, 4096, 65536, 131072};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        original_xrpc = sizes[i];
        allocated = freed = destroyed = creates = 0;
        assert(gfdm_ea3_config_prepare(&config, &original));
        uint32_t result;
        memcpy(&result, devices[2].value, 4);
        assert(result == (sizes[i] > 65536 ? sizes[i] : 65536));
        assert(creates == (sizes[i] >= 65536 ? 0 : 1));
        gfdm_avs_config_fini(&config);
        assert(allocated == freed && destroyed == 1);
    }
    for (failure = 1; failure <= 10; failure++) {
        if (failure != 1 && failure != 2 && failure != 7 && failure != 8 &&
            failure != 9 && failure != 10) continue;
        allocated = freed = destroyed = creates = 0;
        original_xrpc = 4096;
        assert(!gfdm_ea3_config_prepare(&config, &original));
        assert(allocated == freed);
        assert(config.root == NULL && config.property == NULL && config.buffer == NULL);
    }
    puts("PASS: independent V4 AVS/V7 EA3 config, deep clone, exact save paths, XRPC minimum/larger values, original preservation and failure cleanup");
    return 0;
}
