#ifndef GFDMHOOK1_PROFILE_H
#define GFDMHOOK1_PROFILE_H

#include <stdbool.h>

struct gfdm_profile {
    unsigned int version;
    const char *executable;
    const char *mcode_gf;
    const char *mcode_dm;
    bool movie_v4_guard;
};

/* Match only a complete executable basename, independently of its directory. */
const struct gfdm_profile *gfdm_profile_find(const char *path);

#endif
