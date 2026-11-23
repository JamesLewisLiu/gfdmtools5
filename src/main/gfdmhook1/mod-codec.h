#ifndef GFDMHOOK1_MOD_CODEC_H
#define GFDMHOOK1_MOD_CODEC_H
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct gfdm_mod_backend {
    void *context;
    int (*read_base)(void *, unsigned char **, size_t *);
    int (*xml_text)(const unsigned char *, size_t, unsigned char **, size_t *);
    void (*release)(void *);
    void (*log)(int, const char *);
};
bool gfdm_mod_convert(const char *root, const char *relative,
    const struct gfdm_mod_backend *backend, char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
