#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gfdmhook1/profile.c"

int main(void)
{
    const char *gf[] = {"GCG33JAA", "GCH33JAA", "GCI33JAA", "GCJ33JBA", "GCK33JBA"};
    const char *dm[] = {"GCG32JAA", "GCH32JAA", "GCI32JAA", "GCJ32JBA", "GCK32JBA"};
    const char *bad[] = {NULL, "", "gdv3.exe", "gdv9.exe", "gdv80.exe",
                         "gdv5.exe.bak", "othergdv5.exe", "C:/gdv5.exe/game.exe"};
    unsigned int i;

    for (i = 0; i < 5; i++) {
        char path[128];
        const struct gfdm_profile *profile;

        snprintf(path, sizeof(path), "I:\\folder with spaces\\GDV%u.EXE", i + 4);
        profile = gfdm_profile_find(path);
        assert(profile != NULL && profile->version == i + 4);
        assert(strcmp(profile->mcode_gf, gf[i]) == 0);
        assert(strcmp(profile->mcode_dm, dm[i]) == 0);
        assert(profile->movie_v4_guard == (i == 0));
        assert(memcmp(profile->black_sign_key->data,
                      i == 4 ? "SHAMOSAN" : "UDONHRKI", 8) == 0);
        snprintf(path, sizeof(path), "I:/GFDMV%u/binbt5/gdv%u.exe", i + 4, i + 4);
        assert(gfdm_profile_find(path) == profile);
        assert(gfdm_profile_find(profile->executable) == profile);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(gfdm_profile_find(bad[i]) == NULL);
    }
    puts("PASS: version/mode identities, native signing keys, path matching, unsupported executables and V4-only movie guard");
    return 0;
}
