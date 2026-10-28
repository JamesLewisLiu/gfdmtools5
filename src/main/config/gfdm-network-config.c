#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "config/gfdm-network-config.h"
#include "gfdmhook1/display-mode.h"

static const char display_comment[] =
    "# gfx.fullscreen: 0 window, 1 fullscreen 4:3 (default), 2 fullscreen "
    "stretch.\n"
    "# Restart the game after changing the display mode.\n";

static const char adapter_comment[] =
    "# If the game cannot start with multiple network adapters (including "
    "VPNs),\n"
    "# set this to the IPv4 address of the adapter used to reach your server.\n"
    "# Only that adapter is exposed to the game. Leave empty to use the "
    "default route.\n";

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

/* Read complete physical lines so a long comment can never be mistaken for a
 * setting when the next input chunk starts with a key name. */
int gfdm_network_read_line(
    FILE *file, char **line, size_t *capacity, size_t *length)
{
    int ch;
    *length = 0;
    while ((ch = fgetc(file)) != EOF) {
        if (*length + 1 >= *capacity) {
            size_t next = *capacity != 0 ? *capacity * 2 : 512;
            if (next <= *capacity)
                return -1;
            char *buffer = realloc(*line, next);
            if (buffer == NULL)
                return -1;
            *line = buffer;
            *capacity = next;
        }
        (*line)[(*length)++] = (char) ch;
        if (ch == '\n')
            break;
    }
    if (ferror(file))
        return -1;
    if (*length == 0)
        return 0;
    (*line)[*length] = '\0';
    return 1;
}

static void write_comment(FILE *dst, const char *comment, const char *newline)
{
    while (*comment != '\0') {
        const char *end = strchr(comment, '\n');
        size_t length =
            end != NULL ? (size_t) (end - comment) : strlen(comment);
        fwrite(comment, 1, length, dst);
        fputs(newline, dst);
        comment += length + (end != NULL);
    }
}

bool gfdm_network_write_config(
    FILE *src,
    FILE *dst,
    unsigned int version,
    const char *mode,
    const char *server,
    const char *adapter,
    bool keyboard,
    int display_mode)
{
    char display_value[2], mcode[16];
    char *line = NULL;
    size_t capacity = 0, length = 0;
    bool first = true, any_text = false, needs_newline = false;
    bool newline_seen = false;
    const char *newline = "\n";
    int status = 0;

    if (display_mode < 0 || display_mode > 2)
        return false;
    snprintf(display_value, sizeof(display_value), "%d", display_mode);
    snprintf(
        mcode,
        sizeof(mcode),
        "GC%c%dJ%cA",
        'G' + version - 4,
        strcmp(mode, "gf") == 0 ? 33 : 32,
        version >= 7 ? 'B' : 'A');

    struct {
        const char *key, *value, *comment;
        bool editable, seen;
    } settings[] = {
        {"eamuse.server=",
         server,
         "# e-amusement endpoint. Use the host/port where your server "
         "listens.\n",
         true,
         false},
        {"adapter.override_ip=", adapter, adapter_comment, true, false},
        {"eamuse.pcbid=",
         "0101020304050607086F",
         "# System/network PCBID and black roundplug ID.\n",
         false,
         false},
        {"eamuse.eamid=",
         "0101020304050607086F",
         "# White roundplug EAMID.\n",
         false,
         false},
        {"security.mcode=",
         mcode,
         "# Keep this code matched to the game version and GF/DM model.\n",
         false,
         false},
        {"input.keyboard=",
         keyboard ? "true" : "false",
         "# Optional built-in keyboard fallback; disable when using configured "
         "IO.\n",
         true,
         false},
        {"gfx.frame_rate_limit=",
         "60.0",
         "# Keep the OpenGL frame rate at 60 for normal game speed.\n",
         false,
         false},
        {"gfx.forced_refresh_rate=",
         "60",
         "# Requested fullscreen refresh rate in Hz.\n",
         false,
         false},
        {"gfx.fullscreen=", display_value, display_comment, true, false},
    };

    while (src != NULL &&
           (status = gfdm_network_read_line(src, &line, &capacity, &length)) >
               0) {
        size_t offset =
            first && length >= 3 && memcmp(line, "\xEF\xBB\xBF", 3) == 0 ? 3 :
                                                                           0;
        size_t index;
        first = false;
        any_text = true;
        if (!newline_seen && line[length - 1] == '\n') {
            newline = length >= 2 && line[length - 2] == '\r' ? "\r\n" : "\n";
            newline_seen = true;
        }
        for (index = 0; index < sizeof(settings) / sizeof(settings[0]);
             index++) {
            size_t key_length = strlen(settings[index].key);
            if (length - offset < key_length ||
                memcmp(line + offset, settings[index].key, key_length) != 0)
                continue;
            if (!settings[index].editable) {
                settings[index].seen = true;
                break;
            }
            /* The UI loads the last occurrence. Write its selected value at
               the first occurrence and discard only later editable-key lines.
               Comments and blank lines around them remain byte-for-byte. */
            if (!settings[index].seen) {
                size_t suffix = length;
                while (suffix > offset + key_length &&
                       (line[suffix - 1] == '\n' || line[suffix - 1] == '\r' ||
                        line[suffix - 1] == ' ' || line[suffix - 1] == '\t'))
                    suffix--;
                fwrite(line, 1, offset + key_length, dst);
                fputs(settings[index].value, dst);
                fwrite(line + suffix, 1, length - suffix, dst);
                needs_newline = line[length - 1] != '\n';
                settings[index].seen = true;
            }
            break;
        }
        if (index == sizeof(settings) / sizeof(settings[0]) ||
            !settings[index].editable) {
            fwrite(line, 1, length, dst);
            needs_newline = line[length - 1] != '\n';
        }
    }
    free(line);
    if (status < 0)
        return false;

    if (!any_text) {
        fprintf(
            dst,
            "# %s V%u configuration%s%s",
            strcmp(mode, "gf") == 0 ? "Guitar Freaks" : "DrumMania",
            version,
            newline,
            newline);
    }
    for (size_t index = 0; index < sizeof(settings) / sizeof(settings[0]);
         index++) {
        if (settings[index].seen)
            continue;
        if (needs_newline) {
            fputs(newline, dst);
            needs_newline = false;
        }
        /* Explain newly added settings only. Never regenerate, upgrade or
           remove any existing comment, even if it resembles a stock template.
         */
        write_comment(dst, settings[index].comment, newline);
        fprintf(
            dst, "%s%s%s", settings[index].key, settings[index].value, newline);
    }
    return !ferror(dst);
}
