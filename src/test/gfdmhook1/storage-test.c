#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gfdmhook1/storage.c"

static HMODULE patched;

void hook_table_apply(HMODULE target, const char *dependency,
                     const struct hook_symbol *symbols, size_t count)
{
    assert(count == 1);
    if (strcmp(dependency, "kernel32.dll") == 0) {
        assert(strcmp(symbols[0].name, "CreateDirectoryA") == 0);
    } else {
        assert(strcmp(dependency, "libavs-win32.dll") == 0);
        assert(strcmp(symbols[0].name, "avs_fs_mount") == 0);
    }
    patched = target;
}

static LPCSTR captured_path;
static LPSECURITY_ATTRIBUTES captured_attributes;
static const char *captured_mountpoint, *captured_device, *captured_fstype;
static void *captured_data;

static int __cdecl capture_mount(const char *point, const char *device,
                                 const char *type, void *data)
{
    captured_mountpoint = point;
    captured_device = device;
    captured_fstype = type;
    captured_data = data;
    return -42;
}

static BOOL WINAPI capture_create(LPCSTR path, LPSECURITY_ATTRIBUTES attributes)
{
    captured_path = path;
    captured_attributes = attributes;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static void write_marker(const char *path, char marker)
{
    DWORD written;
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(WriteFile(file, &marker, 1, &written, NULL) && written == 1);
    assert(CloseHandle(file));
}

static void check_marker(const char *path, char marker)
{
    DWORD count;
    char actual;
    HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(ReadFile(file, &actual, 1, &count, NULL) && count == 1);
    assert(actual == marker);
    assert(CloseHandle(file));
}

static void remove_mode_directories(void)
{
    assert(RemoveDirectoryA(gfdm_raw_path));
    assert(RemoveDirectoryA(gfdm_nvram_path));
    assert(RemoveDirectoryA(gfdm_conf_path));
    assert(RemoveDirectoryA(gfdm_proxy_path));
    assert(RemoveDirectoryA(gfdm_cache_path));
    assert(RemoveDirectoryA(gfdm_tmp_path));
    assert(RemoveDirectoryA(gfdm_game_path));
}

int main(void)
{
    char temporary[MAX_PATH], directory[MAX_PATH], executable[MAX_PATH];
    char collision[MAX_PATH], long_name[MAX_PATH + 20];
    char gf_marker[MAX_PATH], dm_marker[MAX_PATH], legacy[MAX_PATH];
    char original_cwd[MAX_PATH];
    const char *unchanged[] = {NULL, "", "E:/CONF2", "E:/CONF/M32",
        "E:/CONF/NVRAM/0", "E:CONF", "D:/CONF", "F:/CONF", "E:/TMP", "F:/CACHE2", "../CONF",
        "./CONF/../other", "C:/other/CONF", "./NVRAM", "./RAW"};
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    HANDLE file;

    assert(!gfdm_storage_init(NULL, true));
    assert(gfdm_storage_init((HMODULE) 1, true));
    assert(patched == (HMODULE) 1);
    assert(gfdm_storage_set_paths("I:\\game with spaces\\binbt5\\gdv8.exe", true));
    assert(strcmp(gfdm_storage_redirect("E:/CONF"),
                  "I:\\game with spaces\\binbt5\\DEV\\GF\\CONF") == 0);
    assert(strcmp(gfdm_storage_redirect(".\\conf\\nvram\\"),
                  "I:\\game with spaces\\binbt5\\DEV\\GF\\CONF\\NVRAM") == 0);
    assert(strcmp(gfdm_storage_redirect("e:\\Conf\\Raw"),
                  "I:\\game with spaces\\binbt5\\DEV\\GF\\CONF\\RAW") == 0);
    assert(gfdm_storage_redirect("./CONF") == gfdm_conf_path);
    assert(gfdm_storage_redirect("E:/CONF/NVRAM") == gfdm_nvram_path);
    assert(gfdm_storage_redirect("./CONF/RAW") == gfdm_raw_path);
    assert(gfdm_storage_redirect("F:/TMP") == gfdm_tmp_path);
    assert(gfdm_storage_redirect("./CACHE") == gfdm_cache_path);
    assert(gfdm_storage_redirect("f:\\cache\\proxy") == gfdm_proxy_path);
    for (size_t i = 0; i < lengthof(unchanged); i++) {
        assert(gfdm_storage_redirect(unchanged[i]) == unchanged[i]);
    }
    memset(long_name, 'a', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = '\0';
    assert(gfdm_storage_redirect(long_name) == long_name);
    long_name[0] = 'I'; long_name[1] = ':'; long_name[2] = '\\';
    long_name[sizeof(long_name) - 10] = '\\';
    assert(!gfdm_storage_set_paths(long_name, true));
    assert(!gfdm_storage_set_paths("gdv4.exe", false));
    for (unsigned int version = 4; version <= 8; version++) {
        snprintf(executable, sizeof(executable), "I:/binbt5/gdv%u.exe", version);
        for (unsigned int mode = 0; mode < 2; mode++) {
            char expected[MAX_PATH];
            assert(gfdm_storage_set_paths(executable, mode != 0));
            snprintf(expected, sizeof(expected), "I:/binbt5\\DEV\\%s\\CONF\\NVRAM",
                     mode ? "GF" : "DM");
            assert(strcmp(gfdm_storage_nvram(), expected) == 0);
            snprintf(expected, sizeof(expected), "I:/binbt5\\DEV\\%s\\CONF\\RAW",
                     mode ? "GF" : "DM");
            assert(strcmp(gfdm_storage_raw(), expected) == 0);
        }
    }

    real_CreateDirectoryA = capture_create;
    assert(!gfdm_CreateDirectoryA("E:/CONF", &attributes));
    assert(captured_path == gfdm_conf_path && captured_attributes == &attributes);
    assert(GetLastError() == ERROR_ACCESS_DENIED);
    assert(!gfdm_CreateDirectoryA(unchanged[2], NULL));
    assert(captured_path == unchanged[2] && captured_attributes == NULL);
    assert(GetLastError() == ERROR_ACCESS_DENIED);

    real_avs_fs_mount = capture_mount;
    assert(gfdm_storage_mount("/tmp/", "F:/TMP", "fs", &attributes) == -42);
    assert(strcmp(captured_mountpoint, "/tmp/") == 0);
    assert(captured_device == gfdm_tmp_path);
    assert(strcmp(captured_fstype, "fs") == 0 && captured_data == &attributes);
    assert(gfdm_storage_mount("/proxy/", "F:/CACHE/PROXY", "fs", NULL) == -42);
    assert(captured_device == gfdm_proxy_path && captured_data == NULL);
    assert(gfdm_storage_mount("/", "../", "fs", NULL) == -42);
    assert(strcmp(captured_device, "../") == 0);
    assert(gfdm_storage_mount("/dev/nvram", "NVRAM", "nvram", NULL) == -42);
    assert(strcmp(captured_device, "NVRAM") == 0);

    assert(GetTempPathA(sizeof(temporary), temporary));
    assert(GetTempFileNameA(temporary, "gfs", 0, directory));
    assert(DeleteFileA(directory));
    assert(CreateDirectoryA(directory, NULL));
    assert(snprintf(executable, sizeof(executable), "%s\\gdv5.exe", directory)
           < (int) sizeof(executable));
    assert(GetCurrentDirectoryA(sizeof(original_cwd), original_cwd));
    assert(SetCurrentDirectoryA(temporary));
    assert(gfdm_storage_set_paths(executable, true));
    real_CreateDirectoryA = CreateDirectoryA;
    /* Native boot must still create CONF successfully on first launch. */
    assert(gfdm_storage_prepare_root());
    assert(gfdm_CreateDirectoryA("E:/CONF", NULL));
    assert(gfdm_CreateDirectoryA("./CONF/NVRAM", NULL));
    assert(gfdm_CreateDirectoryA("E:/CONF/RAW", NULL));
    assert(gfdm_storage_prepare());
    assert(!gfdm_CreateDirectoryA("E:/CONF", NULL));
    assert(GetLastError() == ERROR_ALREADY_EXISTS);
    assert(!gfdm_CreateDirectoryA("E:/CONF/NVRAM", NULL));
    assert(GetLastError() == ERROR_ALREADY_EXISTS);
    assert(RemoveDirectoryA(gfdm_nvram_path));
    file = CreateFileA(gfdm_nvram_path, GENERIC_WRITE, 0, NULL,
                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(CloseHandle(file));
    assert(!gfdm_storage_prepare());  /* Existing file must not count as a dir. */
    strcpy(collision, gfdm_nvram_path);
    assert(DeleteFileA(collision));
    assert(gfdm_storage_prepare());
    /* Both modes can retain different files with the same basename. */
    snprintf(gf_marker, sizeof(gf_marker), "%s\\save.bin", gfdm_nvram_path);
    write_marker(gf_marker, 'G');
    snprintf(legacy, sizeof(legacy), "%s\\CONF", directory);
    assert(CreateDirectoryA(legacy, NULL));
    snprintf(legacy, sizeof(legacy), "%s\\CONF\\save.bin", directory);
    write_marker(legacy, 'L');
    assert(gfdm_storage_set_paths(executable, false));
    assert(gfdm_storage_prepare());
    snprintf(dm_marker, sizeof(dm_marker), "%s\\save.bin", gfdm_nvram_path);
    assert(GetFileAttributesA(dm_marker) == INVALID_FILE_ATTRIBUTES);
    write_marker(dm_marker, 'D');
    assert(gfdm_storage_prepare());
    check_marker(gf_marker, 'G');
    check_marker(dm_marker, 'D');
    check_marker(legacy, 'L');
    assert(DeleteFileA(dm_marker));
    remove_mode_directories();
    assert(gfdm_storage_set_paths(executable, true));
    assert(gfdm_storage_prepare());
    check_marker(gf_marker, 'G');
    assert(DeleteFileA(gf_marker));
    remove_mode_directories();
    assert(RemoveDirectoryA(gfdm_dev_path));
    assert(DeleteFileA(legacy));
    snprintf(legacy, sizeof(legacy), "%s\\CONF", directory);
    assert(RemoveDirectoryA(legacy));
    /* A file occupying DEV must fail without writing elsewhere. */
    write_marker(gfdm_dev_path, 'X');
    assert(!gfdm_storage_prepare());
    check_marker(gfdm_dev_path, 'X');
    assert(DeleteFileA(gfdm_dev_path));
    assert(SetCurrentDirectoryA(original_cwd));
    assert(RemoveDirectoryA(directory));
    puts("PASS: V4-V8 GF/DM storage isolation, parent creation before native boot, different CWD, existing saves, legacy preservation, exact redirection/mount scope and file collisions");
    return 0;
}
