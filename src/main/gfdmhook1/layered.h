#ifndef GFDMHOOK1_LAYERED_H
#define GFDMHOOK1_LAYERED_H

#include <stdbool.h>

bool gfdm_layered_init(unsigned int version, bool enabled, bool full_verify);
void gfdm_layered_apply_hooks(void);

#endif
