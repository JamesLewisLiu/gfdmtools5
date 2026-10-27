#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "config/action-order.h"
#include "config/gfdm-network-config.c"
#include "config/schema.c"
#include "config/hid-metadata.c"

static void check_metadata_lifetime(void)
{
    wchar_t text[256] = L"unchanged";
    HANDLE handle = CreateEventW(NULL, FALSE, FALSE, NULL);
    assert(handle != NULL);
    InitializeCriticalSection(&metadata_lock);
    /* Missing registry data must still give a stable label without a USB
       string request. Closing/reusing a handle must not retain that label. */
    cache_device(handle, "\\\\?\\gfdm-config-test-device");
    assert(metadata_product(handle, text, sizeof(text)));
    assert(wcscmp(text, L"\\\\?\\gfdm-config-test-device") == 0);
    assert(!metadata_product(handle, text, 2));
    assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    assert(wcscmp(text, L"\\\\?\\gfdm-config-test-device") == 0);
    assert(!metadata_product(handle, NULL, sizeof(text)));
    assert(!metadata_manufacturer(handle, text, sizeof(text)));
    assert(metadata_close(handle));
    assert(metadata == NULL);
    assert(!metadata_product(handle, text, sizeof(text)));
    assert(GetLastError() == ERROR_NOT_FOUND);
    handle = CreateEventW(NULL, FALSE, FALSE, NULL);
    assert(handle != NULL);
    assert(!metadata_product(handle, text, sizeof(text)));
    assert(metadata_close(handle));
    DeleteCriticalSection(&metadata_lock);
}

static void touch(const wchar_t *directory, const wchar_t *name, bool remove)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", directory, name);
    if (remove) {
        assert(DeleteFileW(path));
        return;
    }
    FILE *file = _wfopen(path, L"wb");
    assert(file != NULL);
    assert(fclose(file) == 0);
}

static void check_text(const char *input, int display_mode)
{
    FILE *src = input != NULL ? tmpfile() : NULL;
    FILE *dst = tmpfile();
    char text[4096];
    assert(dst != NULL);
    if (src != NULL) {
        fputs(input, src);
        rewind(src);
    }
    assert(gfdm_network_write_config(
        src, dst, 7, "gf", "example:1234", "192.0.2.1", true, display_mode));
    rewind(dst);
    text[fread(text, 1, sizeof(text) - 1, dst)] = '\0';
    assert(strstr(text, "eamuse.server=example:1234\n"));
    assert(strstr(text, "adapter.override_ip=192.0.2.1\n"));
    assert(strstr(text, "input.keyboard=true\n"));
    char expected_setting[32];
    snprintf(expected_setting, sizeof(expected_setting), "gfx.fullscreen=%d", display_mode);
    assert(strstr(text, expected_setting));
    const char *setting = strncmp(text, "gfx.fullscreen=", 15) == 0 ? text :
        strstr(text, "\ngfx.fullscreen=");
    assert(setting != NULL && strstr(setting + 1, "\ngfx.fullscreen=") == NULL);
    if (input != NULL && strstr(input, "# User comment") != NULL) {
        assert(strstr(text, "# User comment\r\n"));
        assert(strstr(text, "security.mcode=GCJ33JBA\n"));
        assert(strstr(text, "# Last comment"));
    } else if (input == NULL || input[0] == '\0') {
        assert(strstr(text, "# Guitar Freaks V7 configuration\n"));
        assert(strstr(text, "# Optional built-in keyboard fallback;"));
    }
    if (input != NULL && strstr(input, "# Incomplete configuration") != NULL) {
        assert(strstr(text, "# Incomplete configuration\n"));
        assert(strstr(text, "security.mcode=GCJ33JBA\n"));
    }
    if (input == NULL) {
        assert(strstr(text, "security.mcode=GCJ33JBA\n"));
        assert(strstr(text, "# If the game cannot start with multiple network adapters"));
        assert(strstr(text, "# gfx.fullscreen: 0 window, 1 fullscreen 4:3 (default), 2 fullscreen stretch."));
    } else {
        /* Saving is not a template migration: every physical comment line,
           including old generated text, must survive unchanged. */
        const char *line = input;
        while (*line != '\0') {
            const char *end = strchr(line, '\n');
            size_t length = end != NULL ? (size_t) (end + 1 - line) : strlen(line);
            if (*line == '#') {
                char comment[1024];
                assert(length < sizeof(comment));
                memcpy(comment, line, length);
                comment[length] = '\0';
                assert(strstr(text, comment) != NULL);
            }
            line += length;
        }
    }
    if (src != NULL) {
        fclose(src);
    }
    fclose(dst);
}

static char *rewrite(const char *input)
{
    FILE *src = tmpfile(), *dst = tmpfile();
    assert(src != NULL && dst != NULL);
    assert(fwrite(input, 1, strlen(input), src) == strlen(input));
    rewind(src);
    assert(gfdm_network_write_config(src, dst, 4, "gf", "localhost:80",
        "10.9.0.19", false, 2));
    long size = ftell(dst);
    assert(size >= 0);
    char *text = calloc((size_t) size + 1, 1);
    assert(text != NULL);
    rewind(dst);
    assert(fread(text, 1, (size_t) size, dst) == (size_t) size);
    fclose(src);
    fclose(dst);
    return text;
}

static void check_preservation(void)
{
    const char *base =
        "# Guitar Freaks V4 (G33)\r\n"
        "#eamuse.server=localhost:80\r\n"
        "eamuse.server=localhost:80\r\n"
        "# Optional IPv4 address to expose as the only network adapter to the game.\r\n"
        "# Leave empty to select the adapter used by the default route.\r\n"
        "adapter.override_ip=10.9.0.19\r\n"
        "eamuse.pcbid=0101020304050607086F\r\n"
        "eamuse.eamid=0101020304050607086F\r\n"
        "security.mcode=GCG33JAA\r\n"
        "input.keyboard=false\t \r\n"
        "gfx.frame_rate_limit=60.0\r\n"
        "gfx.forced_refresh_rate=60\r\n"
        "\r\n"
        "# Isolated V4 object-store test\r\n"
        "layered.enabled=true\r\n"
        "# gfx.fullscreen: true for fullscreen, false for a fixed-size window; restart required.\r\n"
        "gfx.fullscreen=2";
    char *text = rewrite(base);
    assert(strcmp(text, base) == 0); /* CRLF, trailing spaces, no final LF */
    free(text);

    /* A duplicate editable key is removed without moving surrounding text. */
    char *duplicate = malloc(strlen(base) + 256);
    assert(duplicate != NULL);
    sprintf(duplicate, "%s\r\n# Keep this duplicate's explanation\r\n"
        "adapter.override_ip=\r\neamuse.server=old\r\n"
        "input.keyboard=true\r\ngfx.fullscreen=0\r\n", base);
    text = rewrite(duplicate);
    char *expected = malloc(strlen(base) + 100);
    assert(expected != NULL);
    sprintf(expected, "%s\r\n# Keep this duplicate's explanation\r\n", base);
    assert(strcmp(text, expected) == 0);
    char *again = rewrite(text);
    assert(strcmp(text, again) == 0);
    free(again);
    free(expected);
    free(duplicate);
    free(text);

    /* Chunk boundaries inside a long comment must never start a fake key. */
    char *long_comment = malloc(strlen(base) + 2000);
    assert(long_comment != NULL);
    memset(long_comment, 'x', 511);
    long_comment[0] = '#';
    strcpy(long_comment + 511, "adapter.override_ip=comment text\n");
    strcat(long_comment, base);
    text = rewrite(long_comment);
    assert(strcmp(text, long_comment) == 0);
    free(text);
    free(long_comment);

    /* A UTF-8 BOM and mixed physical line endings also remain opaque text. */
    char *bom = malloc(strlen(base) + 4);
    assert(bom != NULL);
    strcpy(bom, "\xEF\xBB\xBF");
    strcat(bom, base);
    text = rewrite(bom);
    assert(strcmp(text, bom) == 0);
    free(text);
    free(bom);
}

int main(void)
{
    check_metadata_lifetime();
    check_preservation();
    int display_mode;
    for (int i = 0; i <= 4; i++) {
        char value[] = {(char) ('0' + i), '\0'};
        assert(gfdm_display_parse_mode(value, &display_mode) && display_mode == (i <= 2 ? i : i - 2));
        check_text("# User setting\ngfx.fullscreen=0\n", i <= 2 ? i : i - 2);
    }
    assert(gfdm_display_parse_mode(" true\r\n", &display_mode) && display_mode == 1);
    assert(gfdm_display_parse_mode("FALSE", &display_mode) && display_mode == 0);
    assert(!gfdm_display_parse_mode("5", &display_mode));
    assert(!gfdm_display_parse_mode("-1", &display_mode));
    assert(!gfdm_display_parse_mode("3junk", &display_mode));
    assert(!gfdm_display_parse_mode("2 extra", &display_mode));
    assert(!gfdm_display_parse_mode("", &display_mode));
    wchar_t base[MAX_PATH], directory[MAX_PATH], exe[MAX_PATH], path[MAX_PATH];
    const unsigned int expected[] = {
        IDS_GENERIC_TEST,        IDS_GENERIC_SERVICE,      IDS_GF_P1_RED,
        IDS_GF_P1_GREEN,         IDS_GF_P1_BLUE,           IDS_GF_P1_PICK_A,
        IDS_GF_P1_PICK_B,        IDS_GF_P1_WAIL,           IDS_GF_P2_RED,
        IDS_GF_P2_GREEN,         IDS_GF_P2_BLUE,           IDS_GF_P2_PICK_A,
        IDS_GF_P2_PICK_B,        IDS_GF_P2_WAIL,           IDS_GF_P1_START,
        IDS_GF_P1_EFFECT,        IDS_GF_P1_EFFECT_REVERSE, IDS_GF_P2_START,
        IDS_GF_P2_EFFECT,        IDS_GF_P2_EFFECT_REVERSE, IDS_GF_DEBUG_AUTO,
        IDS_GF_DEBUG_MENU_TIMER, IDS_GENERIC_COIN,
    };
    const struct schema gf = {
        .name = "gf", .actions = gf_actions, .nactions = 23};
    const struct schema dm = {.name = "dm", .nactions = 23};
    bool seen[23] = {false};
    unsigned int version;
    for (size_t row = 0; row < 23; row++) {
        size_t action = gfdm_action_for_display_row(&gf, row);
        assert(action < 23 && !seen[action]);
        assert(gf.actions[action].name_rsrc == expected[row]);
        assert(gfdm_display_row_for_action(&gf, action) == row);
        seen[action] = true;
        assert(gfdm_action_for_display_row(&dm, row) == row);
    }
    /* Historical mapper indices and their game input bits remain stable. */
    assert(gf.actions[2].bit == 0x08 && gf.actions[9].bit == 0x1c);
    assert(gf.actions[21].bit == 0x0a && gf.actions[22].bit == 0x0b);
    assert(GetTempPathW(MAX_PATH, base) != 0);
    assert(GetTempFileNameW(base, L"gfd", 0, directory) != 0);
    assert(DeleteFileW(directory) && CreateDirectoryW(directory, NULL));
    swprintf(exe, MAX_PATH, L"%ls\\config.exe", directory);
    assert(!gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version));
    touch(directory, L"gfdm-v7-gf.conf", false);
    assert(
        gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version) &&
        version == 7);
    touch(directory, L"gdv4.exe", false);
    assert(
        gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version) &&
        version == 7);
    touch(directory, L"gdv7.exe", false);
    assert(
        gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version) &&
        version == 7);
    touch(directory, L"gfdm-v4-gf.conf", false);
    assert(!gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version));
    touch(directory, L"gamestart-v7-gf.bat", false);
    assert(
        gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version) &&
        version == 7);
    touch(directory, L"gamestart-v7-gf.bat", true);
    touch(directory, L"gdv4.exe", true);
    assert(
        gfdm_network_select_path(exe, "gf", path, MAX_PATH, &version) &&
        version == 7);
    touch(directory, L"gfdm-v4-gf.conf", true);
    touch(directory, L"gfdm-v7-gf.conf", true);
    assert(
        gfdm_network_select_path(exe, "dm", path, MAX_PATH, &version) &&
        version == 7);
    assert(wcsstr(path, L"gfdm-v7-dm.conf"));
    touch(directory, L"gdv7.exe", true);
    assert(RemoveDirectoryW(directory));
    check_text(NULL, false);
    check_text("", true);
    check_text(
        "eamuse.server=old\nadapter.override_ip=\ninput.keyboard=false\n", true);
    check_text("# Incomplete configuration\n", false);
    check_text("gfx.fullscreen=false\n", true);
    check_text("gfx.fullscreen=true", false);
    check_text("# gfx.fullscreen: true for fullscreen, false for a fixed-size window; restart required.\ngfx.fullscreen=true\n", 2);
    check_text("# Fullscreen display. Set false for a fixed-size window. Restart the game after changing.\ngfx.fullscreen=false\n", 1);
    check_text("# gfx.fullscreen: 0 window, 1 fullscreen 4:3 (default), 2 fullscreen stretch.\n"
        "# Restart the game after changing the display mode.\ngfx.fullscreen=1\n", 1);
    check_text("# gfx.fullscreen: 0 window, 1 fullscreen 4:3, 2 fullscreen stretch,\n"
        "# 3 borderless 4:3, 4 borderless stretch; restart required.\n"
        "gfx.fullscreen=4\n"
        "# Optional IPv4 address to expose as the only network adapter to the game.\n"
        "# Leave empty to select the adapter used by the default route.\n"
        "adapter.override_ip=\n", 2);
    check_text(
        "# User comment\r\neamuse.server=old\nadapter.override_ip=\n"
        "input.keyboard=false\nsecurity.mcode=GCJ33JBA\n# Last comment", true);
    puts(
        "PASS: package-only V7, mixed executables, ambiguous versions, GF/DM "
        "paths, exact comment/format preservation, editable-key deduplication "
        "and documented missing settings");
    return 0;
}
