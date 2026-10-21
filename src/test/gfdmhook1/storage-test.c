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

int main(void)
{
    char temporary[MAX_PATH], directory[MAX_PATH], executable[MAX_PATH];
    char collision[MAX_PATH], long_name[MAX_PATH + 20];
    const char *unchanged[] = {NULL, "", "E:/CONF2", "E:/CONF/M32",
        "E:/CONF/NVRAM/0", "E:CONF", "D:/CONF", "F:/CONF", "E:/TMP", "F:/CACHE2", "../CONF",
        "./CONF/../other", "C:/other/CONF", "./NVRAM", "./RAW"};
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    HANDLE file;

    assert(!gfdm_storage_init(NULL));
    assert(gfdm_storage_init((HMODULE) 1));
    assert(patched == (HMODULE) 1);
    assert(gfdm_storage_set_paths("I:\\game with spaces\\binbt5\\gdv8.exe"));
    assert(strcmp(gfdm_storage_redirect("E:/CONF"),
                  "I:\\game with spaces\\binbt5\\CONF") == 0);
    assert(strcmp(gfdm_storage_redirect(".\\conf\\nvram\\"),
                  "I:\\game with spaces\\binbt5\\CONF\\NVRAM") == 0);
    assert(strcmp(gfdm_storage_redirect("e:\\Conf\\Raw"),
                  "I:\\game with spaces\\binbt5\\CONF\\RAW") == 0);
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
    assert(!gfdm_storage_set_paths(long_name));
    assert(!gfdm_storage_set_paths("gdv4.exe"));

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
    assert(gfdm_storage_set_paths(executable));
    real_CreateDirectoryA = CreateDirectoryA;
    assert(gfdm_CreateDirectoryA("E:/CONF", NULL));
    assert(gfdm_CreateDirectoryA("./CONF/NVRAM", NULL));
    assert(gfdm_CreateDirectoryA("E:/CONF/RAW", NULL));
    assert(gfdm_storage_prepare());
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
    assert(RemoveDirectoryA(gfdm_raw_path));
    assert(RemoveDirectoryA(gfdm_nvram_path));
    assert(RemoveDirectoryA(gfdm_conf_path));
    assert(RemoveDirectoryA(gfdm_proxy_path));
    assert(RemoveDirectoryA(gfdm_cache_path));
    assert(RemoveDirectoryA(gfdm_tmp_path));
    assert(RemoveDirectoryA(directory));
    puts("PASS: native save path redirection, exact scope, API errors/attributes, real local directories, existing save directories and file collision");
    return 0;
}
