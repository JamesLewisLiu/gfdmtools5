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
   The ABI and record layout below are verified against V4 libpackfs only. */
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
static char layered_root[MAX_PATH];
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
    char path[MAX_PATH];
    uint8_t header[48], sha[32], md5[16];
    uint64_t size;
    FILE *file;
    uint32_t path_offset, pack_offset;
    uint16_t pack_count;
    struct layered_record *sorted;
    bool ok;

    snprintf(path, sizeof(path), "%s/pack/packinfo.bin", layered_root);
    if (!layered_hash_file(path, sha, md5, &size) || size < 16 || size > 8*1024*1024) {
        return false;
    }
    layered_index_size = (uint32_t) size;
    layered_index = malloc(layered_index_size);
    if (layered_index == NULL) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    ok = fread(layered_index, 1, layered_index_size, file) == layered_index_size;
    fclose(file);
    if (!ok || layered_u32(layered_index) != 0x20041207 ||
        !layered_hash_buffer(layered_index, layered_index_size, sha)) return false;
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

    snprintf(path, sizeof(path), "%s/_loose/v4/manifest.bin", layered_root);
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
    return true;
}

/* Only libpackfs's AVS import is wrapped. A packed fallback cannot succeed,
   even if an unexpected AVS mount points at the original data tree. Verify
   the actual mounted index byte-for-byte before native boot consumes it. */
static avs_desc __cdecl layered_open_avs(const char *path, uint16_t mode, int flags)
{
    size_t len = path != NULL ? strlen(path) : 0;
    avs_desc fd;
    if (len >= 4 && _stricmp(path + len - 4, ".pak") == 0) {
        log_warning("GFDM layered BLOCKED packed read: %s", path);
        return 0;
    }
    fd = layered_avs_open(path, mode, flags);
    if (path != NULL && strcmp(path, "/data/pack/packinfo.bin") == 0) {
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

static unsigned int __cdecl layered_open(const char *name)
{
    const struct layered_record *record;
    const struct layered_entry *entry;
    char object[65], path[96];
    unsigned int fd;

    if (name == NULL) return 0;
    record = layered_get_record(name);
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

bool gfdm_layered_init(unsigned int version, bool enabled)
{
    HMODULE pkfs, avs;
    char executable[MAX_PATH], module_path[MAX_PATH], digest[65];
    uint8_t sha[32], md5[16];
    uint64_t size;
    char *slash;
    DWORD len;
    if (!enabled) return true;
    if (version != 4) {
        log_warning("GFDM layered data currently supports V4 only");
        return false;
    }
    pkfs = GetModuleHandleA("libpackfs.dll");
    avs = GetModuleHandleA("libavs-win32.dll");
    if (pkfs == NULL || avs == NULL ||
        !GetModuleFileNameA(pkfs, module_path, sizeof(module_path)) ||
        !layered_hash_file(module_path, sha, md5, &size)) return false;
    layered_hex(digest, sha, 32);
    if (strcmp(digest, "59dc206577ed796df75e7a038108667501751929ecd3530ed972bbac2a309c09")) {
        log_warning("GFDM layered: unsupported libpackfs SHA256 %s", digest);
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
    log_info("GFDM layered: strict V4 objects validated; root=%s records=%u", layered_root, layered_count);
    return true;
}

void gfdm_layered_apply_hooks(void)
{
    if (!layered_enabled) return;
    hook_table_apply(GetModuleHandleA("libpackfs.dll"), "libavs-win32.dll",
                     layered_avs_syms, lengthof(layered_avs_syms));
    hook_table_apply(NULL, "libpackfs.dll", layered_pkfs_syms, lengthof(layered_pkfs_syms));
}
