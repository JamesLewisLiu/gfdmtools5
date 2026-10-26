#include <windows.h>
#include <wincrypt.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfdmhook1/layered.h"
#include "hook/table.h"
#include "imports/avs.h"
#include "util/defs.h"
#include "util/log.h"

/* Independent implementation of the native-handle routing idea in
   ifs_layeredfs. No image/IFS conversion or filename dictionary is needed.
   Native-handle/record ABIs are verified for the five exact modules below. */
struct layered_profile {
    unsigned int version;
    uint32_t magic;
    const char *module_sha;
    const char *index_sha;
    const char *finfolist_sha;
};

static const struct layered_profile layered_profiles[] = {
    {4, 0x20041207,
     "59dc206577ed796df75e7a038108667501751929ecd3530ed972bbac2a309c09",
     "fa24be212d8b5cb4728520109dc380377978bbea0ece5b7572ae2d9169ea6a32",
     "8db66c2c13dafaf456468f5461e1397a37dd12d472820532b1a6fdf6de4606c4"},
    {5, 0x20041207,
     "b71ef9439b7e40f93c775b09eae3b81fdebfd975d333b2f91a1ae15ccc8ffc6c",
     "35b23be339c4ae148d79a391394bbb659e300a48008022416e91ace3a4b5926a",
     "d55c67058bd76ebbcf228679b1d1505d9a5917967dfafcf78bfe6c535876260a"},
    {6, 0x20041207,
     "1f794701b0fc11aff6c74b2e1282933b061c14a2a193410395346f4f0aabe075",
     "1db01448ce1a485a39077d7d417cbc8ea4ae90c8edeb2b1ba287e712ea9d699f",
     "78184f8a63b66d5202efe0ef5c63f1386b21548e58292efa2698bc4dded62390"},
    {7, 0x20041207,
     "a14a9f914026e4d60aa5eed88f3d3a756663ab152240e95371fb7ab21da5e861",
     "2dda31ec1913c1e09905524a68d7d61cea9e909d4eed9d545d012a538718637e",
     "4c389716c53f183747fdaef6cc9479f1e89c604217f3ceacfe1d1c5c530bca98"},
    {8, 0x20110201,
     "8060b37e886c2c1a488eaf102d8a6dafffd5bfd5b653fdf945bcafd474eaad51",
     "1811333342788e139bc640fa2ab432f83da79289213753c862ae41502e26a6e7",
     "766bc3104c1d96b20fdd79c399eadea63d37d564f0f80efc9b63a7b907c3c147"},
};
struct layered_record {
    uint8_t md5[16];
    uint32_t crc32;
    uint16_t crc16;
    uint16_t pack;
    uint32_t offset;
    uint32_t length;
};

struct layered_entry {
    struct layered_record record;
    uint8_t sha256[32];
};

typedef char layered_record_size[(sizeof(struct layered_record) == 32) ? 1 : -1];
typedef char layered_entry_size[(sizeof(struct layered_entry) == 64) ? 1 : -1];

static bool layered_enabled;
static bool layered_index_seen;
static const struct layered_profile *layered_profile;
static char layered_root[MAX_PATH];
static char layered_index_path[MAX_PATH];
static char layered_finfolist_path[MAX_PATH];
static char layered_index_virtual[96];
static char layered_finfolist_virtual[96];
static uint8_t *layered_index;
static uint32_t layered_index_size;
static struct layered_entry *layered_entries;
static uint32_t layered_count;
static unsigned int (__cdecl *layered_real_open)(const char *);
static int (__cdecl *layered_real_boot)(void);
static const struct layered_record *(__cdecl *layered_get_record)(const char *);
static avs_desc (__cdecl *layered_avs_open)(const char *, uint16_t, int);
static size_t (__cdecl *layered_avs_read)(avs_desc, char *, uint32_t);
static int (__cdecl *layered_avs_close)(avs_desc);
static int (__cdecl *layered_avs_seek)(avs_desc, int, int);

static const struct layered_profile *layered_profile_find(unsigned int version)
{
    for (size_t i = 0; i < lengthof(layered_profiles); i++) {
        if (layered_profiles[i].version == version) return &layered_profiles[i];
    }
    return NULL;
}

static bool layered_select_metadata(void)
{
    DWORD index_attributes, finfolist_attributes;
    DWORD index_error, finfolist_error;
    unsigned int version = layered_profile->version;
    snprintf(layered_index_path, sizeof(layered_index_path),
             "%s/_loose/v%u/packinfo.bin", layered_root, version);
    snprintf(layered_finfolist_path, sizeof(layered_finfolist_path),
             "%s/_loose/v%u/finfolist.bin", layered_root, version);
    index_attributes = GetFileAttributesA(layered_index_path);
    index_error = index_attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
    finfolist_attributes = GetFileAttributesA(layered_finfolist_path);
    finfolist_error = finfolist_attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
    if ((index_error && index_error != ERROR_FILE_NOT_FOUND && index_error != ERROR_PATH_NOT_FOUND) ||
        (finfolist_error && finfolist_error != ERROR_FILE_NOT_FOUND && finfolist_error != ERROR_PATH_NOT_FOUND)) return false;
    if (index_attributes != INVALID_FILE_ATTRIBUTES ||
        finfolist_attributes != INVALID_FILE_ATTRIBUTES) {
        /* A partial new layout must never fall back to an unrelated root. */
        if (index_attributes == INVALID_FILE_ATTRIBUTES ||
            finfolist_attributes == INVALID_FILE_ATTRIBUTES ||
            (index_attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (finfolist_attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        snprintf(layered_index_virtual, sizeof(layered_index_virtual),
                 "/data/_loose/v%u/packinfo.bin", version);
        snprintf(layered_finfolist_virtual, sizeof(layered_finfolist_virtual),
                 "/data/_loose/v%u/finfolist.bin", version);
        return true;
    }
    if (version != 4) return false;
    snprintf(layered_index_path, sizeof(layered_index_path),
             "%s/pack/packinfo.bin", layered_root);
    snprintf(layered_finfolist_path, sizeof(layered_finfolist_path),
             "%s/finfolist.bin", layered_root);
    strcpy(layered_index_virtual, "/data/pack/packinfo.bin");
    strcpy(layered_finfolist_virtual, "/data/finfolist.bin");
    return true;
}

static bool layered_is_metadata(const char *path, const char *expected)
{
    char normalized[96];
    size_t len;
    if (path == NULL) return false;
    len = strlen(path);
    if (len >= sizeof(normalized)) return false;
    for (size_t i = 0; i <= len; i++) normalized[i] = path[i] == '\\' ? '/' : path[i];
    const char *start = normalized[0] == '/' ? normalized + 1 : normalized;
    return _stricmp(start, expected + 1) == 0;
}

static const char *layered_metadata_path(const char *path)
{
    if (layered_is_metadata(path, "/data/pack/packinfo.bin")) return layered_index_virtual;
    if (layered_is_metadata(path, "/data/finfolist.bin")) return layered_finfolist_virtual;
    return path;
}

static void layered_hex(char *out, const uint8_t *in, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * size] = 0;
}

static bool layered_hash_file(const char *path, uint8_t sha[32],
                              uint8_t md5[16], uint64_t *size)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hsha = 0, hmd5 = 0;
    uint8_t buffer[65536];
    DWORD got, digest_size;
    bool ok = false;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    *size = 0;
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES,
                               CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hsha) ||
        !CryptCreateHash(provider, CALG_MD5, 0, 0, &hmd5)) {
        goto done;
    }
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) {
            goto done;
        }
        if (got == 0) {
            break;
        }
        if (!CryptHashData(hsha, buffer, got, 0) ||
            !CryptHashData(hmd5, buffer, got, 0)) {
            goto done;
        }
        *size += got;
    }
    digest_size = 32;
    if (!CryptGetHashParam(hsha, HP_HASHVAL, sha, &digest_size, 0)) {
        goto done;
    }
    digest_size = 16;
    ok = CryptGetHashParam(hmd5, HP_HASHVAL, md5, &digest_size, 0) != 0;
done:
    if (hmd5) CryptDestroyHash(hmd5);
    if (hsha) CryptDestroyHash(hsha);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}

static uint32_t layered_u32(const void *p)
{
    uint32_t n;
    memcpy(&n, p, sizeof(n));
    return n;
}

static bool layered_hash_buffer(const void *data, uint32_t size, uint8_t sha[32])
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    DWORD digest_size = 32;
    bool ok = CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES,
                                   CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
              CryptHashData(hash, data, size, 0) &&
              CryptGetHashParam(hash, HP_HASHVAL, sha, &digest_size, 0);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}

static uint16_t layered_u16(const void *p)
{
    uint16_t n;
    memcpy(&n, p, sizeof(n));
    return n;
}

static int layered_compare(const void *a, const void *b)
{
    const struct layered_record *x = a, *y = b;
    if (x->crc32 != y->crc32) return x->crc32 < y->crc32 ? -1 : 1;
    if (x->crc16 != y->crc16) return x->crc16 < y->crc16 ? -1 : 1;
    return 0;
}

static const struct layered_entry *layered_find(const struct layered_record *key)
{
    return bsearch(key, layered_entries, layered_count,
                   sizeof(*layered_entries), layered_compare);
}

static bool layered_load_manifest(void)
{
    char path[MAX_PATH], digest[65];
    uint8_t header[48], sha[32], md5[16];
    uint64_t size;
    FILE *file;
    uint32_t path_offset, pack_offset;
    uint16_t pack_count;
    struct layered_record *sorted;
    bool ok;

    free(layered_index);
    free(layered_entries);
    layered_index = NULL;
    layered_entries = NULL;
    layered_count = 0;
    layered_index_size = 0;
    if (layered_profile == NULL || !layered_select_metadata() ||
        !layered_hash_file(layered_index_path, sha, md5, &size) ||
        size < 16 || size > 8*1024*1024) {
        return false;
    }
    layered_hex(digest, sha, 32);
    if (strcmp(digest, layered_profile->index_sha)) return false;
    layered_index_size = (uint32_t) size;
    layered_index = malloc(layered_index_size);
    if (layered_index == NULL) return false;
    file = fopen(layered_index_path, "rb");
    if (file == NULL) return false;
    ok = fread(layered_index, 1, layered_index_size, file) == layered_index_size;
    fclose(file);
    if (!ok || layered_u32(layered_index) != layered_profile->magic ||
        !layered_hash_buffer(layered_index, layered_index_size, sha)) return false;
    layered_hex(digest, sha, 32);
    if (strcmp(digest, layered_profile->index_sha)) return false;
    path_offset = layered_u32(layered_index + 4);
    pack_offset = layered_u32(layered_index + 8);
    layered_count = layered_u16(layered_index + 12);
    pack_count = layered_u16(layered_index + 14);
    if (path_offset != 16 || layered_count == 0 ||
        pack_offset != 16 + layered_count * 32 ||
        pack_offset + pack_count * 16 != layered_index_size) return false;
    sorted = malloc(layered_count * sizeof(*sorted));
    if (sorted == NULL) return false;
    memcpy(sorted, layered_index + 16, layered_count * sizeof(*sorted));
    qsort(sorted, layered_count, sizeof(*sorted), layered_compare);
    ok = true;
    for (uint32_t i = 0; i < layered_count; i++) {
        if (sorted[i].pack >= pack_count ||
            (i && layered_compare(&sorted[i-1], &sorted[i]) == 0)) ok = false;
    }
    free(sorted);
    if (!ok) return false;

    snprintf(path, sizeof(path), "%s/_loose/v%u/manifest.bin",
             layered_root, layered_profile->version);
    file = fopen(path, "rb");
    if (file == NULL) return false;
    ok = fread(header, 1, sizeof(header), file) == sizeof(header) &&
         memcmp(header, "GFDMLFS1", 8) == 0 && layered_u32(header + 8) == 1 &&
         layered_u32(header + 12) == layered_count &&
         memcmp(header + 16, sha, 32) == 0;
    layered_entries = calloc(layered_count, sizeof(*layered_entries));
    ok = ok && layered_entries != NULL &&
         fread(layered_entries, sizeof(*layered_entries), layered_count, file) == layered_count &&
         fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!ok) return false;
    for (uint32_t i = 0; i < layered_count; i++) {
        if (i && layered_compare(&layered_entries[i-1], &layered_entries[i]) >= 0)
            return false;
    }
    for (uint32_t i = 0; i < layered_count; i++) {
        const struct layered_record *record = (const void *) (layered_index + 16 + i * 32);
        const struct layered_entry *entry = layered_find(record);
        if (entry == NULL || memcmp(&entry->record, record, 32) != 0) return false;
    }
    log_info("GFDM layered: full SHA256/MD5 preflight of %u records", layered_count);
    for (uint32_t i = 0; i < layered_count; i++) {
        char object[65];
        const struct layered_entry *entry = &layered_entries[i];
        layered_hex(object, entry->sha256, 32);
        snprintf(path, sizeof(path), "%s/_loose/objects/%s.bin", layered_root, object);
        if (!layered_hash_file(path, sha, md5, &size) || size != entry->record.length ||
            memcmp(sha, entry->sha256, 32) || memcmp(md5, entry->record.md5, 16)) {
            log_warning("GFDM layered: invalid object %s", object);
            return false;
        }
        if ((i + 1) % 512 == 0) log_info("GFDM layered preflight: %u/%u", i + 1, layered_count);
    }
    if (!layered_hash_file(layered_finfolist_path, sha, md5, &size)) return false;
    layered_hex(digest, sha, 32);
    if (strcmp(digest, layered_profile->finfolist_sha)) return false;
    return true;
}

/* Every AVS consumer sees the current version's two exact metadata paths.
   Call the export directly, never another import that might be wrapped. */
static avs_desc __cdecl layered_open_metadata(const char *path, uint16_t mode, int flags)
{
    return layered_avs_open(layered_metadata_path(path), mode, flags);
}

/* The libpackfs-specific import additionally forbids packed fallback and
   binds its actual mounted index. Other metadata consumers cannot change
   index_seen, and repeated hook application cannot form a wrapper chain. */
static avs_desc __cdecl layered_open_avs(const char *path, uint16_t mode, int flags)
{
    size_t len = path != NULL ? strlen(path) : 0;
    avs_desc fd;
    if (len >= 4 && _stricmp(path + len - 4, ".pak") == 0) {
        log_warning("GFDM layered BLOCKED packed read: %s", path);
        return 0;
    }
    fd = layered_open_metadata(path, mode, flags);
    if (layered_is_metadata(path, "/data/pack/packinfo.bin")) {
        uint8_t buffer[4096];
        uint32_t offset = 0;
        bool ok = fd != 0;
        layered_index_seen = false;
        while (ok && offset < layered_index_size) {
            uint32_t want = layered_index_size - offset;
            if (want > sizeof(buffer)) want = sizeof(buffer);
            size_t got = layered_avs_read(fd, (char *) buffer, want);
            ok = got == want && memcmp(buffer, layered_index + offset, want) == 0;
            offset += want;
        }
        if (ok) ok = layered_avs_read(fd, (char *) buffer, 1) == 0;
        if (ok) ok = layered_avs_seek(fd, 0, 0) == 0;
        if (!ok) {
            if (fd) layered_avs_close(fd);
            log_warning("GFDM layered: mounted packinfo differs from preflight");
            return 0;
        }
        layered_index_seen = true;
        log_info("GFDM layered: mounted index verified (%u bytes)", layered_index_size);
    }
    return fd;
}

static int __cdecl layered_boot(void)
{
    int result;
    layered_index_seen = false;
    result = layered_real_boot();
    if (result != 0 || !layered_index_seen) {
        log_fatal("GFDM layered: native PKFS initialization failed (%d)", result);
    }
    log_info("GFDM layered: native PKFS ready; packed fallback forbidden");
    return result;
}

static const char *layered_lookup_name(const char *name, char normalized[512])
{
    if (layered_profile->version < 7) return name;
    /* Match the exact V7/V8 native open helper, not a general path cleanup.
       Its exported lookup does not normalize. Native snprintf truncates to
       511 bytes, and the special lowercase prefix is /data/aep, without
       /product. Backslashes and all other bytes remain unchanged. */
    snprintf(normalized, 512, _strnicmp(name, "data/", 5) == 0 ? "/%s" : "%s", name);
    if (_strnicmp(normalized, "/data/aep", 9) == 0) {
        for (char *p = normalized; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
        }
    }
    return normalized;
}

static unsigned int __cdecl layered_open(const char *name)
{
    const struct layered_record *record;
    const struct layered_entry *entry;
    char object[65], path[96], normalized[512];
    unsigned int fd;

    if (name == NULL) return 0;
    record = layered_get_record(layered_lookup_name(name, normalized));
    if (record == NULL) {
        /* Genuine loose resources retain native semantics. No new name is
           added to the original index and writes are never redirected. */
        fd = layered_real_open(name);
        log_misc("GFDM layered loose: fd=%u name=%s", fd, name);
        return fd;
    }
    entry = layered_find(record);
    if (entry == NULL || memcmp(record, &entry->record, sizeof(*record)) != 0) {
        log_warning("GFDM layered: unmapped native record for %s", name);
        return 0;
    }
    layered_hex(object, entry->sha256, 32);
    snprintf(path, sizeof(path), "/data/_loose/objects/%s.bin", object);
    if (layered_get_record(path) != NULL) {
        log_warning("GFDM layered: object path collides with packed index");
        return 0;
    }
    fd = layered_real_open(path);
    log_info("GFDM layered object: fd=%u key=%08x:%04x bytes=%u sha256=%s name=%s",
             fd, record->crc32, record->crc16, record->length, object, name);
    return fd;
}

static const struct hook_symbol layered_pkfs_syms[] = {
    {.name = "?pkfs_fs_open@@YAIPBD@Z", .patch = layered_open},
    {.name = "?pkfs_boot@@YAHXZ", .patch = layered_boot},
};
static const struct hook_symbol layered_avs_syms[] = {
    {.name = "avs_fs_open", .patch = layered_open_avs},
};
static const struct hook_symbol layered_metadata_syms[] = {
    {.name = "avs_fs_open", .patch = layered_open_metadata},
};

bool gfdm_layered_init(unsigned int version, bool enabled)
{
    HMODULE pkfs, avs;
    char executable[MAX_PATH], module_path[MAX_PATH], digest[65];
    uint8_t sha[32], md5[16];
    uint64_t size;
    char *slash;
    DWORD len;
    if (!enabled) return true;
    layered_profile = layered_profile_find(version);
    if (layered_profile == NULL) {
        log_warning("GFDM layered: unsupported version %u", version);
        return false;
    }
    pkfs = GetModuleHandleA("libpackfs.dll");
    avs = GetModuleHandleA("libavs-win32.dll");
    if (pkfs == NULL || avs == NULL ||
        !GetModuleFileNameA(pkfs, module_path, sizeof(module_path)) ||
        !layered_hash_file(module_path, sha, md5, &size)) return false;
    layered_hex(digest, sha, 32);
    if (strcmp(digest, layered_profile->module_sha)) {
        log_warning("GFDM layered V%u: unsupported libpackfs SHA256 %s", version, digest);
        return false;
    }
    len = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (!len || len >= sizeof(executable)) return false;
    slash = strrchr(executable, '\\');
    if (!slash) return false;
    *slash = 0;
    if (strlen(executable) + sizeof("/../data/_loose/objects/") + 69 >= MAX_PATH) return false;
    snprintf(module_path, sizeof(module_path), "%s/../data", executable);
    len = GetFullPathNameA(module_path, sizeof(layered_root), layered_root, NULL);
    if (!len || len >= sizeof(layered_root)) return false;
    layered_real_open = (void *) GetProcAddress(pkfs, "?pkfs_fs_open@@YAIPBD@Z");
    layered_real_boot = (void *) GetProcAddress(pkfs, "?pkfs_boot@@YAHXZ");
    layered_get_record = (void *) GetProcAddress(pkfs, "?pkfs_get_pathtab@@YAPAUpkfs_pathtab@@PBD@Z");
    layered_avs_open = (void *) GetProcAddress(avs, "avs_fs_open");
    layered_avs_read = (void *) GetProcAddress(avs, "avs_fs_read");
    layered_avs_close = (void *) GetProcAddress(avs, "avs_fs_close");
    layered_avs_seek = (void *) GetProcAddress(avs, "avs_fs_lseek");
    if (!layered_real_open || !layered_real_boot || !layered_get_record ||
        !layered_avs_open || !layered_avs_read || !layered_avs_close || !layered_avs_seek ||
        !layered_load_manifest()) return false;
    layered_enabled = true;
    log_info("GFDM layered: strict V%u objects validated; root=%s records=%u",
             version, layered_root, layered_count);
    return true;
}

void gfdm_layered_apply_hooks(void)
{
    if (!layered_enabled) return;
    hook_table_apply(NULL, "libavs-win32.dll",
                     layered_metadata_syms, lengthof(layered_metadata_syms));
    hook_table_apply(GetModuleHandleA("libpackfs.dll"), "libavs-win32.dll",
                     layered_avs_syms, lengthof(layered_avs_syms));
    hook_table_apply(NULL, "libpackfs.dll", layered_pkfs_syms, lengthof(layered_pkfs_syms));
}
