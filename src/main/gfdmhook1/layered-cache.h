#ifndef GFDM_LAYERED_CACHE_H
#define GFDM_LAYERED_CACHE_H

/* Private to layered.c. The cache is a local acceleration hint, not an
   authenticated catalog. Expected hashes always come from the bound manifest.
   Only unchanged local NTFS file identities may reuse a previous verification. */
#define LAYERED_CACHE_LIMIT 65536u
#define LAYERED_CACHE_SLOTS 131072u
struct layered_cache_record {
    uint8_t sha[32], md5[16];
    uint32_t identity[12];
};
typedef char layered_cache_record_size[(sizeof(struct layered_cache_record) == 96) ? 1 : -1];
struct layered_cache {
    struct layered_cache_record *records;
    uint32_t *slots;
    bool *verified;
    uint32_t count, hits, hashes;
    uint64_t bytes;
    char path[MAX_PATH];
};
static uint32_t layered_last_cache_hits, layered_last_hashes;
static DWORD layered_last_preflight_ms;
static uint64_t layered_last_hash_bytes;

static bool layered_file_identity(HANDLE file, uint32_t identity[12])
{
    /* Resolve these APIs dynamically so older systems still fully verify. */
    typedef BOOL (WINAPI *basic_fn)(HANDLE, int, void *, DWORD);
    typedef BOOL (WINAPI *volume_fn)(HANDLE, LPWSTR, DWORD, LPDWORD,
                                   LPDWORD, LPDWORD, LPWSTR, DWORD);
    struct { LARGE_INTEGER created, accessed, written, changed; DWORD attributes; } basic;
    BY_HANDLE_FILE_INFORMATION info;
    wchar_t fs[16];
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    basic_fn get_basic = (basic_fn) (void *) GetProcAddress(kernel, "GetFileInformationByHandleEx");
    volume_fn get_volume = (volume_fn) (void *) GetProcAddress(kernel, "GetVolumeInformationByHandleW");
    if (!get_basic || !get_volume || GetFileType(file) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(file, &info) ||
        !get_basic(file, 0 /* FileBasicInfo */, &basic, sizeof(basic)) ||
        !get_volume(file, NULL, 0, NULL, NULL, NULL, fs, lengthof(fs)) ||
        wcscmp(fs, L"NTFS") != 0 || !basic.changed.QuadPart ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    identity[0] = info.dwVolumeSerialNumber;
    identity[1] = info.nFileIndexHigh; identity[2] = info.nFileIndexLow;
    identity[3] = info.nFileSizeHigh; identity[4] = info.nFileSizeLow;
    identity[5] = info.ftCreationTime.dwLowDateTime; identity[6] = info.ftCreationTime.dwHighDateTime;
    identity[7] = info.ftLastWriteTime.dwLowDateTime; identity[8] = info.ftLastWriteTime.dwHighDateTime;
    identity[9] = basic.changed.LowPart; identity[10] = (uint32_t) basic.changed.HighPart;
    identity[11] = info.dwFileAttributes;
    return true;
}

static uint32_t layered_cache_slot(struct layered_cache *cache, const uint8_t sha[32])
{
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < 32; i++) hash = (hash ^ sha[i]) * 16777619u;
    uint32_t slot = hash & (LAYERED_CACHE_SLOTS - 1);
    while (cache->slots[slot] &&
           memcmp(cache->records[cache->slots[slot] - 1].sha, sha, 32))
        slot = (slot + 1) & (LAYERED_CACHE_SLOTS - 1);
    return slot;
}

static void layered_cache_begin(struct layered_cache *cache)
{
    uint8_t header[48], sha[32];
    memset(cache, 0, sizeof(*cache));
    cache->records = calloc(LAYERED_CACHE_LIMIT, sizeof(*cache->records));
    cache->slots = calloc(LAYERED_CACHE_SLOTS, sizeof(*cache->slots));
    cache->verified = calloc(LAYERED_CACHE_LIMIT, sizeof(*cache->verified));
    if (!cache->records || !cache->slots || !cache->verified) return;
    if (snprintf(cache->path, sizeof(cache->path), "%s/_loose/verified-cache.bin", layered_root) >= (int) sizeof(cache->path)) {
        cache->path[0] = 0; return;
    }
    FILE *file = fopen(cache->path, "rb");
    if (!file) return;
    bool ok = fread(header, 1, 48, file) == 48 && !memcmp(header, "GFLFC001", 8) &&
        layered_u32(header + 8) == 1;
    uint32_t count = ok ? layered_u32(header + 12) : 0;
    ok = ok && count <= LAYERED_CACHE_LIMIT &&
        fread(cache->records, sizeof(*cache->records), count, file) == count &&
        fgetc(file) == EOF && !ferror(file) &&
        layered_hash_buffer(cache->records, count * sizeof(*cache->records), sha) &&
        !memcmp(header + 16, sha, 32);
    fclose(file);
    for (uint32_t i = 0; ok && i < count; i++) {
        uint32_t slot = layered_cache_slot(cache, cache->records[i].sha);
        if (cache->slots[slot]) ok = false;
        else cache->slots[slot] = i + 1;
    }
    if (ok) cache->count = count;
    else {
        memset(cache->slots, 0, LAYERED_CACHE_SLOTS * sizeof(*cache->slots));
        log_warning("GFDM layered: unusable verification cache; rebuilding from contents");
    }
}

static bool layered_cache_verify(struct layered_cache *cache, const char *path,
                                 const struct layered_entry *entry, bool force)
{
    uint32_t identity[12], slot = 0, index = 0;
    uint8_t sha[32], md5[16];
    uint64_t size = 0;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool eligible = cache->records && cache->slots && cache->verified &&
        cache->path[0] && layered_file_identity(file, identity);
    if (eligible) {
        slot = layered_cache_slot(cache, entry->sha256);
        index = cache->slots[slot] ? cache->slots[slot] - 1 : cache->count;
        if (cache->slots[slot] && (!force || cache->verified[index]) &&
            !memcmp(cache->records[index].identity, identity, sizeof(identity)) &&
            !memcmp(cache->records[index].md5, entry->record.md5, 16) &&
            identity[3] == 0 && identity[4] == entry->record.length) {
            cache->hits++; CloseHandle(file); return true;
        }
    }
    /* This same read handle denies concurrent writers/deleters throughout
       metadata comparison and hashing. Never cache a merely existing file. */
    bool ok = layered_hash_handle(file, sha, md5, &size) &&
        size == entry->record.length && !memcmp(sha, entry->sha256, 32) &&
        !memcmp(md5, entry->record.md5, 16);
    cache->hashes++; cache->bytes += size;
    if (ok && eligible && index < LAYERED_CACHE_LIMIT && layered_file_identity(file, identity)) {
        memcpy(cache->records[index].sha, sha, 32);
        memcpy(cache->records[index].md5, md5, 16);
        memcpy(cache->records[index].identity, identity, sizeof(identity));
        cache->verified[index] = true;
        if (!cache->slots[slot]) { cache->slots[slot] = index + 1; cache->count++; }
    }
    CloseHandle(file);
    return ok;
}

static void layered_cache_end(struct layered_cache *cache, bool success)
{
    uint8_t header[48] = "GFLFC001";
    char temporary[MAX_PATH];
    /* Atomic replacement: failures only cost the next launch a full scan.
       Concurrent writers may lose cache hits, never validation requirements. */
    if (success && cache->path[0] && cache->count && cache->records && cache->slots && cache->verified &&
        snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", cache->path, (unsigned long) GetCurrentProcessId()) < (int) sizeof(temporary) &&
        layered_hash_buffer(cache->records, cache->count * sizeof(*cache->records), header + 16)) {
        uint32_t version = 1;
        memcpy(header + 8, &version, 4); memcpy(header + 12, &cache->count, 4);
        HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            bool ok = WriteFile(file, header, sizeof(header), &written, NULL) && written == sizeof(header) &&
                WriteFile(file, cache->records, cache->count * sizeof(*cache->records), &written, NULL) &&
                written == cache->count * sizeof(*cache->records) && FlushFileBuffers(file);
            CloseHandle(file);
            if (ok) ok = MoveFileExA(temporary, cache->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!ok) DeleteFileA(temporary);
        }
    }
    layered_last_cache_hits = cache->hits;
    layered_last_hashes = cache->hashes;
    layered_last_hash_bytes = cache->bytes;
    free(cache->records); free(cache->slots); free(cache->verified);
}
#endif
