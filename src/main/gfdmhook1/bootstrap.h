#ifndef GFDMHOOK1_BOOTSTRAP_H
#define GFDMHOOK1_BOOTSTRAP_H

#include "gfdmhook1/profile.h"
#include <stdbool.h>

/* Serve the native launcher's configuration from process-lifetime memory. */
bool gfdm_bootstrap_init(const struct gfdm_profile *profile, bool guitar);

#endif
