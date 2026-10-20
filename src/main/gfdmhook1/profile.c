#include <stddef.h>
#include <string.h>

#include "gfdmhook1/profile.h"

static const struct gfdm_profile profiles[] = {
    {4, "gdv4.exe", "GCG33JAA", "GCG32JAA", true},
    {5, "gdv5.exe", "GCH33JAA", "GCH32JAA", false},
    {6, "gdv6.exe", "GCI33JAA", "GCI32JAA", false},
    {7, "gdv7.exe", "GCJ33JBA", "GCJ32JBA", false},
    {8, "gdv8.exe", "GCK33JBA", "GCK32JBA", false},
};

const struct gfdm_profile *gfdm_profile_find(const char *path)
{
    const char *name;
    const char *p;
    size_t i;

    if (path == NULL) {
        return NULL;
    }
    name = path;
    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        if (_stricmp(name, profiles[i].executable) == 0) {
            return &profiles[i];
        }
    }
    return NULL;
}
