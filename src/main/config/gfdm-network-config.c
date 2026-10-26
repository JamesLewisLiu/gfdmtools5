#include <string.h>
#include <windows.h>

#include "config/gfdm-network-config.h"

static bool regular_file(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
        !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool gfdm_network_select_path(
    const wchar_t *executable,
    const char *mode,
    wchar_t *path,
    size_t count,
    unsigned int *version)
{
    wchar_t candidate[MAX_PATH];
    wchar_t *name;
    size_t remaining;
    unsigned int configs = 0, games = 0, matches = 0, launchers = 0;
    unsigned int config_version = 0, game_version = 0, match_version = 0;
    unsigned int launcher_version = 0;

    if (executable == NULL || mode == NULL || path == NULL || version == NULL ||
        count == 0 || wcslen(executable) >= MAX_PATH ||
        (strcmp(mode, "gf") != 0 && strcmp(mode, "dm") != 0)) {
        return false;
    }
    wcscpy(candidate, executable);
    name = wcsrchr(candidate, L'\\');
    if (name == NULL) {
        name = wcsrchr(candidate, L'/');
    }
    name = name != NULL ? name + 1 : candidate;
    remaining = MAX_PATH - (name - candidate);
    if (remaining < 26) {
        return false;
    }
    for (unsigned int v = 4; v <= 8; v++) {
        bool config, game;
        swprintf(name, remaining, L"gfdm-v%u-%hs.conf", v, mode);
        config = regular_file(candidate);
        swprintf(name, remaining, L"gdv%u.exe", v);
        game = regular_file(candidate);
        swprintf(name, remaining, L"gamestart-v%u-%hs.bat", v, mode);
        if (regular_file(candidate)) {
            launchers++;
            launcher_version = v;
        }
        if (config) {
            configs++;
            config_version = v;
        }
        if (game) {
            games++;
            game_version = v;
        }
        if (config && game) {
            matches++;
            match_version = v;
        }
    }

    /* A package identifies its version even when tested without game binaries.
     */
    if (launchers == 1) {
        *version = launcher_version;
    } else if (configs == 1) {
        *version = config_version;
    } else if (configs > 1 && matches == 1) {
        *version = match_version;
    } else if (configs == 0 && games == 1) {
        *version = game_version;
    } else {
        return false;
    }
    swprintf(name, remaining, L"gfdm-v%u-%hs.conf", *version, mode);
    if (wcslen(candidate) >= count) {
        return false;
    }
    wcscpy(path, candidate);
    return true;
}

bool gfdm_network_write_config(
    FILE *src,
    FILE *dst,
    unsigned int version,
    const char *mode,
    const char *server,
    const char *adapter,
    bool keyboard,
    bool fullscreen)
{
    char line[512];
    bool comments = false, server_seen = false, adapter_seen = false;
    bool keyboard_seen = false, pcbid_seen = false, eamid_seen = false;
    bool mcode_seen = false, frame_limit_seen = false, refresh_seen = false;
    bool needs_newline = false;
    bool fullscreen_seen = false;

    if (src != NULL) {
        while (fgets(line, sizeof(line), src) != NULL) {
            if (line[0] == '#') {
                comments = true;
            }
        }
        if (ferror(src) || fseek(src, 0, SEEK_SET) != 0) {
            return false;
        }
    }
    if (!comments) {
        fprintf(
            dst,
            "# %s V%u configuration\n"
            "# eamuse.server: Asphyxia CORE host and port.\n"
            "# adapter.override_ip: optional IPv4 adapter override; leave "
            "empty for the default route.\n"
            "# eamuse.pcbid/eamid: roundplug IDs; defaults are valid BT5 test "
            "IDs.\n"
            "# security.mcode: keep the version and GF/DM model matched to the "
            "game image.\n"
            "# input.keyboard: built-in fallback, disabled when using "
            "configured IO.\n"
            "# gfx.frame_rate_limit: keep 60 for normal game speed.\n"
            "# gfx.fullscreen: true for fullscreen, false for a fixed-size window; restart required.\n"
            "# gfx.forced_refresh_rate: requested display refresh rate.\n\n",
            strcmp(mode, "gf") == 0 ? "Guitar Freaks" : "DrumMania",
            version);
    }
    if (src != NULL) {
        while (fgets(line, sizeof(line), src) != NULL) {
            size_t len = strlen(line);
            needs_newline = len != 0 && line[len - 1] != '\n';
            if (strncmp(line, "eamuse.server=", 14) == 0) {
                fprintf(dst, "eamuse.server=%s\n", server);
                server_seen = true;
                needs_newline = false;
            } else if (strncmp(line, "adapter.override_ip=", 20) == 0) {
                fprintf(dst, "adapter.override_ip=%s\n", adapter);
                adapter_seen = true;
                needs_newline = false;
            } else if (strncmp(line, "input.keyboard=", 15) == 0) {
                fprintf(
                    dst, "input.keyboard=%s\n", keyboard ? "true" : "false");
                keyboard_seen = true;
                needs_newline = false;
            } else if (strncmp(line, "gfx.fullscreen=", 15) == 0) {
                fprintf(dst, "gfx.fullscreen=%s\n", fullscreen ? "true" : "false");
                fullscreen_seen = true;
                needs_newline = false;
            } else if (strncmp(line, "eamuse.pcbid=", 13) == 0) {
                fputs(line, dst);
                pcbid_seen = true;
            } else if (strncmp(line, "eamuse.eamid=", 13) == 0) {
                fputs(line, dst);
                eamid_seen = true;
            } else if (strncmp(line, "security.mcode=", 15) == 0) {
                fputs(line, dst);
                mcode_seen = true;
            } else if (strncmp(line, "gfx.frame_rate_limit=", 21) == 0) {
                fputs(line, dst);
                frame_limit_seen = true;
            } else if (strncmp(line, "gfx.forced_refresh_rate=", 24) == 0) {
                fputs(line, dst);
                refresh_seen = true;
            } else {
                fputs(line, dst);
            }
        }
        if (ferror(src)) {
            return false;
        }
    }
    if (needs_newline) {
        fputc('\n', dst);
    }
    if (!server_seen) {
        fprintf(dst, "eamuse.server=%s\n", server);
    }
    if (!adapter_seen) {
        fprintf(dst, "adapter.override_ip=%s\n", adapter);
    }
    if (!keyboard_seen) {
        fprintf(dst, "input.keyboard=%s\n", keyboard ? "true" : "false");
    }
    if (!fullscreen_seen) {
        if (comments) {
            fputs("# gfx.fullscreen: true for fullscreen, false for a fixed-size window; restart required.\n", dst);
        }
        fprintf(dst, "gfx.fullscreen=%s\n", fullscreen ? "true" : "false");
    }
    if (!pcbid_seen) {
        fputs("eamuse.pcbid=0101020304050607086F\n", dst);
    }
    if (!eamid_seen) {
        fputs("eamuse.eamid=0101020304050607086F\n", dst);
    }
    if (!mcode_seen) {
        fprintf(
            dst,
            "security.mcode=GC%c%dJ%cA\n",
            'G' + version - 4,
            strcmp(mode, "gf") == 0 ? 33 : 32,
            version >= 7 ? 'B' : 'A');
    }
    if (!frame_limit_seen) {
        fputs("gfx.frame_rate_limit=60.0\n", dst);
    }
    if (!refresh_seen) {
        fputs("gfx.forced_refresh_rate=60\n", dst);
    }
    return !ferror(dst);
}
