#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "config/action-order.h"
#include "config/gfdm-network-config.c"
#include "config/schema.c"

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
    snprintf(expected_setting, sizeof(expected_setting), "gfx.fullscreen=%d\n", display_mode);
    assert(strstr(text, expected_setting));
    assert(strstr(text, "# 3 borderless 4:3, 4 borderless stretch; restart required."));
    assert(!strstr(text, "# gfx.fullscreen: true for fullscreen"));
    const char *setting = strstr(text, "\ngfx.fullscreen=");
    assert(setting != NULL && strstr(setting + 1, "\ngfx.fullscreen=") == NULL);
    if (input != NULL && strstr(input, "# User comment") != NULL) {
        assert(strstr(text, "# User comment\r\n"));
        assert(strstr(text, "security.mcode=GCJ33JBA\n"));
        assert(strstr(text, "# Last comment\n"));
    } else if (input == NULL || input[0] != '#') {
        assert(strstr(text, "# Guitar Freaks V7 configuration\n"));
        assert(strstr(text, "# input.keyboard:"));
    }
    if (input != NULL && strstr(input, "# Incomplete configuration") != NULL) {
        assert(strstr(text, "# Incomplete configuration\n"));
        assert(strstr(text, "security.mcode=GCJ33JBA\n"));
    }
    if (input == NULL) {
        assert(strstr(text, "security.mcode=GCJ33JBA\n"));
    }
    if (src != NULL) {
        fclose(src);
    }
    fclose(dst);
}

int main(void)
{
    int display_mode;
    for (int i = 0; i <= 4; i++) {
        char value[] = {(char) ('0' + i), '\0'};
        assert(gfdm_display_parse_mode(value, &display_mode) && display_mode == i);
        check_text("# User setting\ngfx.fullscreen=0\n", i);
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
    check_text(
        "eamuse.server=old\nadapter.override_ip=\ninput.keyboard=false\n", true);
    check_text("# Incomplete configuration\n", false);
    check_text("gfx.fullscreen=false\n", true);
    check_text("gfx.fullscreen=true", false);
    check_text("# gfx.fullscreen: true for fullscreen, false for a fixed-size window; restart required.\ngfx.fullscreen=true\n", 4);
    check_text("# Fullscreen display. Set false for a fixed-size window. Restart the game after changing.\ngfx.fullscreen=false\n", 3);
    check_text(
        "# User comment\r\neamuse.server=old\nadapter.override_ip=\n"
        "input.keyboard=false\nsecurity.mcode=GCJ33JBA\n# Last comment", true);
    puts(
        "PASS: package-only V7, mixed executables, ambiguous versions, GF/DM "
        "paths, preserved and regenerated comments");
    return 0;
}
