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
        assert(count == 1 && (symbols[0].patch == layered_open_avs ||
                             symbols[0].patch == layered_open_metadata));
        if (symbols[0].patch == layered_open_metadata) assert(target == NULL);
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
static unsigned int fixture_version = 4;
static bool fixture_versioned;
static struct layered_profile fixture_profile;
static char fixture_index_sha[65], fixture_finfolist_sha[65];
static char fixture_index_relative[64], fixture_manifest_relative[64];
static char fixture_finfolist_relative[64];
static const char finfolist[] = "version-specific native finfolist";

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
    layered_profile = NULL;
}

static void write_fixture(void)
{
    char path[MAX_PATH], digest[65];
    uint8_t index_sha[32], ignored[16];
    uint64_t size;
    release_loaded();
    strcpy(layered_root, test_root);
    fixture_profile = *layered_profile_find(fixture_version);
    fixture_profile.index_sha = fixture_index_sha;
    fixture_profile.finfolist_sha = fixture_finfolist_sha;
    layered_profile = &fixture_profile;
    snprintf(fixture_manifest_relative, sizeof(fixture_manifest_relative),
             "_loose/v%u/manifest.bin", fixture_version);
    if (fixture_versioned) {
        snprintf(fixture_index_relative, sizeof(fixture_index_relative),
                 "_loose/v%u/packinfo.bin", fixture_version);
        snprintf(fixture_finfolist_relative, sizeof(fixture_finfolist_relative),
                 "_loose/v%u/finfolist.bin", fixture_version);
    } else {
        strcpy(fixture_index_relative, "pack/packinfo.bin");
        strcpy(fixture_finfolist_relative, "finfolist.bin");
    }
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
        records[i].offset = fixture_version * 64 + i * 32;
    }
    records[0].crc32 = 1;
    records[0].crc16 = 2;
    records[1].crc32 = 0xffffffff;
    records[1].crc16 = 0xffff;
    memset(source_index, 0, sizeof(source_index));
    put_u32(source_index, fixture_profile.magic);
    put_u32(source_index + 4, 16);
    put_u32(source_index + 8, 80);
    put_u16(source_index + 12, 2);
    put_u16(source_index + 14, 1);
    /* Original index order is independent of the manifest's unsigned sort. */
    memcpy(source_index + 16, &records[1], 32);
    memcpy(source_index + 48, &records[0], 32);
    root_file(path, sizeof(path), fixture_index_relative);
    write_bytes(path, source_index, sizeof(source_index));
    assert(layered_hash_file(path, index_sha, ignored, &size));
    layered_hex(fixture_index_sha, index_sha, 32);
    root_file(path, sizeof(path), fixture_finfolist_relative);
    write_bytes(path, finfolist, sizeof(finfolist));
    assert(layered_hash_file(path, index_sha, ignored, &size));
    layered_hex(fixture_finfolist_sha, index_sha, 32);
    assert(layered_hash_buffer(source_index, sizeof(source_index), index_sha));
    memset(binary_manifest, 0, sizeof(binary_manifest));
    memcpy(binary_manifest, "GFDMLFS1", 8);
    put_u32(binary_manifest + 8, 1);
    put_u32(binary_manifest + 12, 2);
    memcpy(binary_manifest + 16, index_sha, 32);
    for (unsigned int i = 0; i < 2; i++) {
        memcpy(binary_manifest + 48 + i * 64, &records[i], 32);
        memcpy(binary_manifest + 80 + i * 64, object_sha, 32);
    }
    root_file(path, sizeof(path), fixture_manifest_relative);
    write_bytes(path, binary_manifest, sizeof(binary_manifest));
}

static void rewrite_manifest(size_t size)
{
    char path[MAX_PATH];
    root_file(path, sizeof(path), fixture_manifest_relative);
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
    root_file(path, sizeof(path), fixture_manifest_relative);
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
    root_file(path, sizeof(path), fixture_index_relative);
    write_bytes(path, source_index, sizeof(source_index));
    assert(layered_hash_file(path, sha, md5, &size));
    layered_hex(fixture_index_sha, sha, 32);
    memcpy(binary_manifest + 16, sha, 32);
    rewrite_manifest(sizeof(binary_manifest));
    assert(!layered_load_manifest());
}

static char opened_name[256];
static unsigned int native_calls;
static bool collision;
static struct layered_record mismatched;
static bool return_mismatch;
static const char *expected_lookup;

static const struct layered_record *__cdecl fake_get_record(const char *path)
{
    if (strcmp(path, expected_lookup != NULL ? expected_lookup : "/known/resource") == 0)
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
    assert(path != NULL && strlen(path) < sizeof(opened_name));
    strcpy(opened_name, path);
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
    layered_index_seen = false;
    assert(layered_open_metadata("/data/pack/packinfo.bin", 0, 420) == 0x12345678);
    assert(!layered_index_seen); /* A foreign consumer cannot acknowledge boot. */
    assert(layered_open_avs("/data/pack/packinfo.bin", 1, 0) == 0x12345678);
    assert(layered_index_seen && stream_offset == 0 && avs_seeks == 1);
    assert(layered_open_metadata("/data/finfolist.bin", 0, 420) == 0x12345678);
    assert(layered_index_seen); /* Nor can it reset a valid PKFS observation. */
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

static void check_version_metadata(void)
{
    char path[MAX_PATH], expected[96];
    const char *bad_index_sha = "wrong-version-index";
    fixture_versioned = true;
    for (fixture_version = 4; fixture_version <= 8; fixture_version++) {
        write_fixture();
        assert(layered_load_manifest());
        assert(layered_profile->version == fixture_version);
        snprintf(expected, sizeof(expected), "/data/_loose/v%u/packinfo.bin", fixture_version);
        assert(strcmp(layered_index_virtual, expected) == 0);
        layered_avs_open = fake_avs_open;
        layered_index_seen = false;
        assert(layered_open_metadata("/DATA\\PACK\\PACKINFO.BIN", 0x9876, -123));
        assert(!strcmp(opened_name, expected));
        assert(!layered_index_seen && captured_mode == 0x9876 && captured_flags == -123);
        snprintf(expected, sizeof(expected), "/data/_loose/v%u/finfolist.bin", fixture_version);
        assert(layered_open_metadata("data/finfolist.bin", 0, 420));
        assert(!strcmp(opened_name, expected));
        assert(layered_open_metadata("/data/product/finfolist.bin", 0, 420));
        assert(!strcmp(opened_name, "/data/product/finfolist.bin"));

        /* Matching header magic alone cannot authorize another version's index. */
        fixture_profile.index_sha = bad_index_sha;
        assert(!layered_load_manifest());
        fixture_profile.index_sha = fixture_index_sha;
        fixture_profile.magic ^= 1;
        assert(!layered_load_manifest());
        fixture_profile.magic ^= 1;
        fixture_profile.finfolist_sha = "wrong-version-finfolist";
        assert(!layered_load_manifest());
        fixture_profile.finfolist_sha = fixture_finfolist_sha;

        root_file(path, sizeof(path), fixture_finfolist_relative);
        assert(DeleteFileA(path));
        assert(!layered_select_metadata()); /* Partial metadata cannot use root files. */
        root_file(path, sizeof(path), fixture_index_relative); assert(DeleteFileA(path));
        if (fixture_version != 4) assert(!layered_select_metadata());
        root_file(path, sizeof(path), fixture_manifest_relative); assert(DeleteFileA(path));
    }
    fixture_version = 5; fixture_versioned = false;
    write_fixture();
    assert(!layered_load_manifest()); /* V5 must not take V4-style root metadata. */
    root_file(path, sizeof(path), fixture_manifest_relative); assert(DeleteFileA(path));
    fixture_version = 4;
    write_fixture(); assert(layered_load_manifest()); /* V4 legacy remains supported. */
}

static void check_native_name_normalization(void)
{
    char path[MAX_PATH], normalized[512], long_name[600];
    fixture_versioned = true;
    for (fixture_version = 4; fixture_version <= 8; fixture_version++) {
        write_fixture(); assert(layered_load_manifest());
        layered_get_record = fake_get_record;
        layered_real_open = fake_native_open;
        native_calls = 0; collision = return_mismatch = false;
        expected_lookup = fixture_version >= 7 ? "/data/aep/upper_case.bin" : "DATA/AEP/UPPER_CASE.BIN";
        assert(layered_open("DATA/AEP/UPPER_CASE.BIN") == 0x76543210);
        assert(strncmp(opened_name, "/data/_loose/objects/", 21) == 0);
        expected_lookup = fixture_version >= 7 ? "/data/product/aep/UPPER_CASE.BIN" : "data/product/aep/UPPER_CASE.BIN";
        assert(layered_open("data/product/aep/UPPER_CASE.BIN") == 0x76543210);
        assert(strncmp(opened_name, "/data/_loose/objects/", 21) == 0);
        expected_lookup = "/data/aep/known.bin";
        assert(layered_open("/data/aep/known.bin") == 0x76543210);
        assert(strncmp(opened_name, "/data/_loose/objects/", 21) == 0);
        expected_lookup = NULL;
        assert(strcmp(layered_lookup_name("data\\AEP\\X", normalized), "data\\AEP\\X") == 0);
        memset(long_name, 'X', sizeof(long_name));
        memcpy(long_name, "data/", 5); long_name[sizeof(long_name)-1] = 0;
        const char *actual = layered_lookup_name(long_name, normalized);
        assert(strlen(actual) == (fixture_version >= 7 ? 511 : sizeof(long_name)-1));
        if (fixture_version >= 7) assert(strncmp(actual, "/data/", 6) == 0);
        root_file(path, sizeof(path), fixture_index_relative); assert(DeleteFileA(path));
        root_file(path, sizeof(path), fixture_finfolist_relative); assert(DeleteFileA(path));
        root_file(path, sizeof(path), fixture_manifest_relative); assert(DeleteFileA(path));
    }
    fixture_version = 4; fixture_versioned = false;
    write_fixture(); assert(layered_load_manifest());
}

static void check_verification_cache(void)
{
    char path[MAX_PATH];
    FILETIME created, accessed, written;
    struct layered_cache cache;
    uint32_t identity[12];
    write_fixture();
    root_file(path, sizeof(path), "_loose/verified-cache.bin");
    DeleteFileA(path);
    assert(layered_load_manifest());
    HANDLE file = CreateFileA(object_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(layered_file_identity(file, identity));
    assert(GetFileTime(file, &created, &accessed, &written)); CloseHandle(file);
    assert(layered_last_hashes == 1 && layered_last_cache_hits == 1);
    assert(layered_load_manifest());
    assert(layered_last_hashes == 0 && layered_last_cache_hits == 2);
    layered_cache_begin(&cache);
    struct layered_entry alternate = layered_entries[0];
    alternate.record.crc32 ^= 0x12345678; alternate.record.offset += 100;
    assert(layered_cache_verify(&cache, object_path, &alternate, false));
    assert(cache.hits == 1 && cache.hashes == 0); /* Shared across version records. */
    layered_cache_end(&cache, true);
    layered_full_verify = true;
    assert(layered_load_manifest());
    assert(layered_last_hashes == 1); /* Deduplicate within this scan. */
    layered_full_verify = false;

    /* A same-size overwrite with restored write time must still miss the
       cache: NTFS ChangeTime is part of the identity, unlike LastAccessTime. */
    Sleep(20);
    char changed[sizeof(payload)]; memcpy(changed, payload, sizeof(payload)); changed[0] ^= 1;
    write_bytes(object_path, changed, sizeof(payload) - 1);
    file = CreateFileA(object_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE && SetFileTime(file, &created, NULL, &written)); CloseHandle(file);
    assert(!layered_load_manifest());
    assert(layered_last_hashes == 1);
    write_fixture(); assert(layered_load_manifest());

    /* Replacement with identical contents rehashes because its file ID is
       different even if the caller restores creation and write times. */
    assert(DeleteFileA(object_path));
    write_bytes(object_path, payload, sizeof(payload) - 1);
    file = CreateFileA(object_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE && SetFileTime(file, &created, NULL, &written)); CloseHandle(file);
    assert(layered_load_manifest()); assert(layered_last_hashes == 1);
    assert(DeleteFileA(object_path)); assert(!layered_load_manifest());
    write_fixture(); assert(layered_load_manifest());

    /* Corrupt/truncated caches are only hints: rebuild, do not bless files. */
    write_bytes(path, "broken", 6);
    assert(layered_load_manifest()); assert(layered_last_hashes == 1);
    assert(layered_load_manifest()); assert(layered_last_hashes == 0);
    FILE *f = fopen(path, "r+b"); assert(f); assert(!fseek(f, 48, SEEK_SET));
    int byte = fgetc(f); assert(byte != EOF && !fseek(f, 48, SEEK_SET));
    assert(fputc(byte ^ 1, f) != EOF && !fclose(f));
    assert(layered_load_manifest()); assert(layered_last_hashes == 1);

    /* A concurrent writer cannot race metadata comparison and content reads. */
    layered_cache_begin(&cache);
    file = CreateFileA(object_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(!layered_cache_verify(&cache, object_path, &layered_entries[0], false));
    CloseHandle(file); layered_cache_end(&cache, false);
    /* No writable cache is required for correctness. */
    assert(DeleteFileA(path)); assert(CreateDirectoryA(path, NULL));
    assert(layered_load_manifest()); assert(layered_last_hashes == 1);
    assert(RemoveDirectoryA(path));
    puts("PASS: persistent cache hits, full override, same-size restored-time corruption, replacement, missing object, corrupt cache, writer exclusion and unwritable cache");
}

int main(int argc, char **argv)
{
    /* CI-built evidence helper, deliberately absent from game packages.
       Uses the production data validator; never loads or patches a game. */
    if (argc == 5 && strcmp(argv[1], "--benchmark-store") == 0) {
        unsigned int version = (unsigned int) atoi(argv[3]);
        layered_profile = layered_profile_find(version);
        assert(layered_profile && strlen(argv[2]) < sizeof(layered_root));
        assert(!strcmp(argv[4], "full") || !strcmp(argv[4], "cached"));
        strcpy(layered_root, argv[2]); layered_full_verify = !strcmp(argv[4], "full");
        DWORD began = GetTickCount();
        bool ok = layered_load_manifest();
        printf("{\"version\":%u,\"pass\":%s,\"ms\":%lu,\"cached\":%u,\"hashed\":%u,\"bytes\":%llu}\n",
               version, ok ? "true" : "false", (unsigned long) (GetTickCount() - began),
               layered_last_cache_hits, layered_last_hashes, (unsigned long long) layered_last_hash_bytes);
        return ok ? 0 : 1;
    }
    assert(argc == 1);
    char temporary[MAX_PATH], path[MAX_PATH];
    const char *directories[] = {"pack", "_loose", "_loose/v4", "_loose/v5",
        "_loose/v6", "_loose/v7", "_loose/v8", "_loose/objects"};
    assert(GetTempPathA(sizeof(temporary), temporary));
    assert(GetTempFileNameA(temporary, "gfl", 0, test_root));
    assert(DeleteFileA(test_root)); assert(CreateDirectoryA(test_root, NULL));
    for (size_t i = 0; i < lengthof(directories); i++) {
        root_file(path, sizeof(path), directories[i]); assert(CreateDirectoryA(path, NULL));
    }
    assert(layered_profile_find(3) == NULL && layered_profile_find(9) == NULL);
    for (unsigned int version = 4; version <= 8; version++) {
        assert(gfdm_layered_init(version, false, false));
        assert(layered_profile_find(version)->version == version);
    }
    assert(!gfdm_layered_init(9, true, false));
    hook_calls = 0; gfdm_layered_apply_hooks(); assert(hook_calls == 0);
    check_manifest_validation();
    check_native_routing();
    check_mounted_index_and_strictpak();
    check_version_metadata();
    check_native_name_normalization();
    check_verification_cache();
    layered_enabled = true; gfdm_layered_apply_hooks(); assert(hook_calls == 3);
    release_loaded();
    assert(DeleteFileA(object_path));
    root_file(path, sizeof(path), "_loose/verified-cache.bin"); DeleteFileA(path);
    root_file(path, sizeof(path), "pack/packinfo.bin"); assert(DeleteFileA(path));
    root_file(path, sizeof(path), "finfolist.bin"); assert(DeleteFileA(path));
    root_file(path, sizeof(path), "_loose/v4/manifest.bin"); assert(DeleteFileA(path));
    for (size_t i = lengthof(directories); i > 0; i--) {
        root_file(path, sizeof(path), directories[i - 1]); assert(RemoveDirectoryA(path));
    }
    assert(RemoveDirectoryA(test_root));
    puts("PASS: V4-V8 version metadata isolation, fail-closed profiles, V4 legacy, full manifest/object verification, index mount binding, strict PAK guard, native handles, collisions and disabled scope");
    return 0;
}
