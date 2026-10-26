#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "gfdmhook1/layered.c"

static jmp_buf fatal_jump;
static bool expect_fatal;
static unsigned int fatal_calls;

static void capture_fatal(const char *module, const char *format, ...)
{
    (void) module;
    (void) format;
    fatal_calls++;
    assert(expect_fatal);
    longjmp(fatal_jump, 1);
}

log_formatter_t log_impl_fatal = capture_fatal;

static unsigned int hook_calls;
void hook_table_apply(HMODULE target, const char *dependency,
                     const struct hook_symbol *symbols, size_t count)
{
    (void) target;
    if (strcmp(dependency, "libavs-win32.dll") == 0) {
        assert(count == 1 && symbols[0].patch == layered_open_avs);
        assert(strcmp(symbols[0].name, "avs_fs_open") == 0);
    } else {
        assert(strcmp(dependency, "libpackfs.dll") == 0 && count == 2);
        assert(symbols[0].patch == layered_open && symbols[1].patch == layered_boot);
    }
    hook_calls++;
}

static char test_root[MAX_PATH], object_path[MAX_PATH];
static uint8_t source_index[96], binary_manifest[176];
static struct layered_record records[2];
static uint8_t object_sha[32], object_md5[16];
static const char payload[] = "raw resource bytes";

static void put_u32(uint8_t *p, uint32_t n) { memcpy(p, &n, 4); }
static void put_u16(uint8_t *p, uint16_t n) { memcpy(p, &n, 2); }

static void write_bytes(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(data, 1, size, f) == size);
    assert(fclose(f) == 0);
}

static void root_file(char *path, size_t capacity, const char *relative)
{
    assert(snprintf(path, capacity, "%s/%s", test_root, relative) < (int) capacity);
}

static void release_loaded(void)
{
    free(layered_index);
    free(layered_entries);
    layered_index = NULL;
    layered_entries = NULL;
    layered_index_size = 0;
    layered_count = 0;
    layered_index_seen = false;
    layered_enabled = false;
}

static void write_fixture(void)
{
    char path[MAX_PATH], digest[65];
    uint8_t index_sha[32], ignored[16];
    uint64_t size;
    release_loaded();
    strcpy(layered_root, test_root);
    root_file(path, sizeof(path), "object-input.bin");
    write_bytes(path, payload, sizeof(payload) - 1);
    assert(layered_hash_file(path, object_sha, object_md5, &size));
    assert(size == sizeof(payload) - 1);
    assert(DeleteFileA(path));
    layered_hex(digest, object_sha, 32);
    assert(snprintf(object_path, sizeof(object_path), "%s/_loose/objects/%s.bin",
                    test_root, digest) < (int) sizeof(object_path));
    write_bytes(object_path, payload, sizeof(payload) - 1);
    memset(records, 0, sizeof(records));
    for (unsigned int i = 0; i < 2; i++) {
        memcpy(records[i].md5, object_md5, 16);
        records[i].length = sizeof(payload) - 1;
        records[i].offset = i * 32;
    }
    records[0].crc32 = 1;
    records[0].crc16 = 2;
    records[1].crc32 = 0xffffffff;
    records[1].crc16 = 0xffff;
    memset(source_index, 0, sizeof(source_index));
    put_u32(source_index, 0x20041207);
    put_u32(source_index + 4, 16);
    put_u32(source_index + 8, 80);
    put_u16(source_index + 12, 2);
    put_u16(source_index + 14, 1);
    /* Original index order is independent of the manifest's unsigned sort. */
    memcpy(source_index + 16, &records[1], 32);
    memcpy(source_index + 48, &records[0], 32);
    root_file(path, sizeof(path), "pack/packinfo.bin");
    write_bytes(path, source_index, sizeof(source_index));
    assert(layered_hash_file(path, index_sha, ignored, &size));
    memset(binary_manifest, 0, sizeof(binary_manifest));
    memcpy(binary_manifest, "GFDMLFS1", 8);
    put_u32(binary_manifest + 8, 1);
    put_u32(binary_manifest + 12, 2);
    memcpy(binary_manifest + 16, index_sha, 32);
    for (unsigned int i = 0; i < 2; i++) {
        memcpy(binary_manifest + 48 + i * 64, &records[i], 32);
        memcpy(binary_manifest + 80 + i * 64, object_sha, 32);
    }
    root_file(path, sizeof(path), "_loose/v4/manifest.bin");
    write_bytes(path, binary_manifest, sizeof(binary_manifest));
}

static void rewrite_manifest(size_t size)
{
    char path[MAX_PATH];
    root_file(path, sizeof(path), "_loose/v4/manifest.bin");
    write_bytes(path, binary_manifest, size);
}

static void check_manifest_validation(void)
{
    char path[MAX_PATH];
    uint8_t sha[32], md5[16];
    uint64_t size;
    write_fixture();
    assert(layered_load_manifest());
    assert(layered_count == 2 && layered_find(&records[1]) != NULL);

    write_fixture(); binary_manifest[0] ^= 1; rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
    write_fixture(); put_u32(binary_manifest + 8, 2); rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
    write_fixture(); put_u32(binary_manifest + 12, 1); rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
    write_fixture(); binary_manifest[16] ^= 1; rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
    write_fixture(); rewrite_manifest(sizeof(binary_manifest) - 1);
    assert(!layered_load_manifest());
    write_fixture();
    root_file(path, sizeof(path), "_loose/v4/manifest.bin");
    FILE *f = fopen(path, "ab"); assert(f); assert(fputc(0, f) == 0); assert(!fclose(f));
    assert(!layered_load_manifest());
    write_fixture(); memcpy(binary_manifest + 112, binary_manifest + 48, 64);
    rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
    write_fixture(); binary_manifest[48 + 24] ^= 1; rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest()); /* record byte differs from original index */

    write_fixture(); write_bytes(object_path, "corrupt", 7);
    assert(!layered_load_manifest());
    write_fixture(); write_bytes(object_path, "XXXXXXXXXXXXXXXXXX", sizeof(payload) - 1);
    assert(!layered_load_manifest()); /* same length, wrong SHA256 and MD5 */
    write_fixture(); assert(DeleteFileA(object_path));
    assert(!layered_load_manifest());
    write_fixture(); binary_manifest[80] ^= 1; rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest()); /* SHA names a missing object */

    /* A malformed original index with duplicate keys must not use an unused
       extra manifest key to pass a one-way membership check. */
    write_fixture();
    memcpy(source_index + 48, source_index + 16, 32);
    root_file(path, sizeof(path), "pack/packinfo.bin");
    write_bytes(path, source_index, sizeof(source_index));
    assert(layered_hash_file(path, sha, md5, &size));
    memcpy(binary_manifest + 16, sha, 32);
    rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
}

static char opened_name[256];
static unsigned int native_calls;
static bool collision;
static struct layered_record mismatched;
static bool return_mismatch;

static const struct layered_record *__cdecl fake_get_record(const char *path)
{
    if (strcmp(path, "/known/resource") == 0)
        return return_mismatch ? &mismatched : &records[0];
    if (collision && strncmp(path, "/data/_loose/objects/", 21) == 0)
        return &records[1];
    return NULL;
}

static unsigned int __cdecl fake_native_open(const char *path)
{
    assert(strlen(path) < sizeof(opened_name));
    strcpy(opened_name, path);
    native_calls++;
    return 0x76543210;
}

static void check_native_routing(void)
{
    char digest[65], expected[96];
    write_fixture(); assert(layered_load_manifest());
    layered_get_record = fake_get_record;
    layered_real_open = fake_native_open;
    collision = false; return_mismatch = false; native_calls = 0;
    assert(layered_open(NULL) == 0 && native_calls == 0);
    assert(layered_open("/unknown/loose") == 0x76543210);
    assert(!strcmp(opened_name, "/unknown/loose") && native_calls == 1);
    assert(layered_open("/known/resource") == 0x76543210);
    layered_hex(digest, object_sha, 32);
    snprintf(expected, sizeof(expected), "/data/_loose/objects/%s.bin", digest);
    assert(!strcmp(opened_name, expected) && native_calls == 2);
    collision = true;
    assert(layered_open("/known/resource") == 0 && native_calls == 2);
    collision = false; return_mismatch = true; mismatched = records[0]; mismatched.offset++;
    assert(layered_open("/known/resource") == 0 && native_calls == 2);
    return_mismatch = false;
}

static unsigned int avs_opens, avs_closes, avs_seeks;
static uint32_t stream_offset;
static uint8_t stream_data[97];
static size_t stream_size;
static bool fail_seek, short_read;
static uint16_t captured_mode;
static int captured_flags;

static avs_desc __cdecl fake_avs_open(const char *path, uint16_t mode, int flags)
{
    (void) path;
    avs_opens++; stream_offset = 0; captured_mode = mode; captured_flags = flags;
    return 0x12345678;
}

static size_t __cdecl fake_avs_read(avs_desc fd, char *buffer, uint32_t want)
{
    assert(fd == 0x12345678);
    size_t available = stream_size - stream_offset;
    if (want > available) want = (uint32_t) available;
    if (short_read && want > 1) want--;
    memcpy(buffer, stream_data + stream_offset, want); stream_offset += want;
    return want;
}

static int __cdecl fake_avs_close(avs_desc fd)
{
    assert(fd == 0x12345678); avs_closes++; return 0;
}

static int __cdecl fake_avs_seek(avs_desc fd, int offset, int whence)
{
    assert(fd == 0x12345678 && offset == 0 && whence == 0);
    avs_seeks++; stream_offset = 0; return fail_seek ? -1 : 0;
}

static int boot_mode;
static int __cdecl fake_boot(void)
{
    if (boot_mode == 0) assert(layered_open_avs("/data/pack/packinfo.bin", 1, 0));
    return boot_mode == 2 ? -7 : 0;
}

static void check_mounted_index_and_strictpak(void)
{
    write_fixture(); assert(layered_load_manifest());
    layered_avs_open = fake_avs_open; layered_avs_read = fake_avs_read;
    layered_avs_close = fake_avs_close; layered_avs_seek = fake_avs_seek;
    avs_opens = avs_closes = avs_seeks = 0;
    fail_seek = short_read = false;
    memcpy(stream_data, source_index, sizeof(source_index)); stream_size = sizeof(source_index);
    assert(!layered_open_avs("/data/pack/d000/pack0000.pak", 1, 0));
    assert(!layered_open_avs("/other/PACK.PAK", 1, 0));
    assert(avs_opens == 0);
    assert(layered_open_avs("/other/loose.bin", 0x9876, -123) == 0x12345678);
    assert(captured_mode == 0x9876 && captured_flags == -123);
    assert(layered_open_avs("/data/pack/packinfo.bin", 1, 0) == 0x12345678);
    assert(layered_index_seen && stream_offset == 0 && avs_seeks == 1);
    stream_data[40] ^= 1;
    assert(!layered_open_avs("/data/pack/packinfo.bin", 1, 0));
    assert(!layered_index_seen && avs_closes == 1);
    memcpy(stream_data, source_index, sizeof(source_index)); stream_size--;
    assert(!layered_open_avs("/data/pack/packinfo.bin", 1, 0));
    stream_size = sizeof(source_index) + 1; stream_data[96] = 0;
    assert(!layered_open_avs("/data/pack/packinfo.bin", 1, 0));
    stream_size = sizeof(source_index); short_read = true;
    assert(!layered_open_avs("/data/pack/packinfo.bin", 1, 0));
    short_read = false; fail_seek = true;
    assert(!layered_open_avs("/data/pack/packinfo.bin", 1, 0));
    fail_seek = false;
    layered_real_boot = fake_boot; boot_mode = 0;
    assert(layered_boot() == 0);
    for (boot_mode = 1; boot_mode <= 2; boot_mode++) {
        expect_fatal = true;
        if (setjmp(fatal_jump) == 0) { layered_boot(); assert(!"missing expected fatal"); }
        expect_fatal = false;
    }
    assert(fatal_calls == 2);
}

int main(void)
{
    char temporary[MAX_PATH], path[MAX_PATH];
    const char *directories[] = {"pack", "_loose", "_loose/v4", "_loose/objects"};
    assert(GetTempPathA(sizeof(temporary), temporary));
    assert(GetTempFileNameA(temporary, "gfl", 0, test_root));
    assert(DeleteFileA(test_root)); assert(CreateDirectoryA(test_root, NULL));
    for (size_t i = 0; i < lengthof(directories); i++) {
        root_file(path, sizeof(path), directories[i]); assert(CreateDirectoryA(path, NULL));
    }
    assert(gfdm_layered_init(4, false));
    for (unsigned int version = 5; version <= 8; version++) assert(!gfdm_layered_init(version, true));
    hook_calls = 0; gfdm_layered_apply_hooks(); assert(hook_calls == 0);
    check_manifest_validation();
    check_native_routing();
    check_mounted_index_and_strictpak();
    layered_enabled = true; gfdm_layered_apply_hooks(); assert(hook_calls == 2);
    release_loaded();
    assert(DeleteFileA(object_path));
    root_file(path, sizeof(path), "pack/packinfo.bin"); assert(DeleteFileA(path));
    root_file(path, sizeof(path), "_loose/v4/manifest.bin"); assert(DeleteFileA(path));
    for (size_t i = lengthof(directories); i > 0; i--) {
        root_file(path, sizeof(path), directories[i - 1]); assert(RemoveDirectoryA(path));
    }
    assert(RemoveDirectoryA(test_root));
    puts("PASS: V4 layered full manifest/object verification, index mount binding, strict PAK guard, native handles and routing, collisions, boot failure and disabled/version scopes");
    return 0;
}
