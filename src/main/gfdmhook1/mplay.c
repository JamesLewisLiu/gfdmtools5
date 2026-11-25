#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gfdmhook1/mplay.h"
#include "util/defs.h"
#include "util/log.h"

enum {
    MPLAY_IMAGE_BASE = 0x10000000,
    MPLAY_IMAGE_SIZE = 0x14e000,
    MPLAY_CODE_START = 0x4fe4,
    MPLAY_CODE_END = 0x57a9,
    MPLAY_THUNK_SIZE = 16,
};

/* Official IDA: V5 libshare-pj SHA256
   ab06c88e9347bbfe862e02c983f7b0ede80c77dd1c5a241d128eeb0069f18ca1.
   Each window includes complete original instructions. Absolute operands
   are relocated before comparison. No branch targets an overwritten byte. */
static const struct mplay_site {
    unsigned int rva, size, offset, instruction_size;
    uint8_t bytes[41];
    uint8_t relocations[5];
} mplay_sites[] = {
    {0x4fe4, 39, 0, 7, /* BGM stop, then MOV and JNS guarding the assertion. */
     {0x83, 0x05, 0x60, 0x9f, 0x04, 0x10, 0xff,
      0x8b, 0x0d, 0x60, 0x9f, 0x04, 0x10, 0x79, 0x18,
      0x68, 0xdb, 0x20, 0x03, 0x10, 0x68, 0x80, 0x05, 0x00, 0x00,
      0x68, 0x90, 0x22, 0x03, 0x10, 0xff, 0x15, 0x3c, 0x02, 0x03, 0x10,
      0x83, 0xc4, 0x0c}, {2, 9, 16, 26, 32}},
    {0x5130, 24, 0, 7, /* System movie stop touches the same counter. */
     {0x83, 0x05, 0x60, 0x9f, 0x04, 0x10, 0xff,
      0x68, 0xa0, 0x25, 0x03, 0x10, 0x68, 0x88, 0x22, 0x03, 0x10,
      0xff, 0xd7, 0x83, 0xc4, 0x08, 0xeb, 0x5c}, {2, 8, 13}},
    {0x5780, 41, 19, 6, /* Successful type 2 enqueue, EAX=1. */
     {0xb8, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0xa8, 0x9b, 0x04, 0x10,
      0x83, 0x7e, 0x04, 0x02, 0x8b, 0xfa, 0x75, 0x06,
      0x01, 0x05, 0x60, 0x9f, 0x04, 0x10, 0x33, 0xc0,
      0x83, 0x3d, 0xa4, 0x9b, 0x04, 0x10, 0xff, 0x75, 0x05,
      0xa3, 0xa0, 0x9b, 0x04, 0x10}, {7, 21, 29, 37}},
};

static void mplay_expected(const struct mplay_site *site, const uint8_t *base,
                           uint8_t *expected)
{
    memcpy(expected, site->bytes, site->size);
    for (size_t i = 0; i < lengthof(site->relocations) && site->relocations[i]; i++) {
        uint8_t *operand = expected + site->relocations[i];
        uint32_t address;
        memcpy(&address, operand, sizeof(address));
        address += (uint32_t)(uintptr_t)base - MPLAY_IMAGE_BASE;
        memcpy(operand, &address, sizeof(address));
    }
}

/* In PE32 a rel32 can reach the entire 32-bit address space. */
static void mplay_jump(uint8_t *bytes, const uint8_t *from, const uint8_t *to)
{
    uint32_t displacement = (uint32_t)(uintptr_t)to - (uint32_t)(uintptr_t)from - 5;
    bytes[0] = 0xe9;
    memcpy(bytes + 1, &displacement, sizeof(displacement));
}

static bool mplay_matches(HMODULE module)
{
    const uint8_t *base = (const uint8_t *)module;
    const IMAGE_DOS_HEADER *dos = (const void *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 0x1000 - (LONG)sizeof(IMAGE_NT_HEADERS32)) {
        return false;
    }
    const IMAGE_NT_HEADERS32 *nt = (const void *)(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        nt->FileHeader.TimeDateStamp == 0x491aba19 &&
        nt->OptionalHeader.SizeOfImage == MPLAY_IMAGE_SIZE &&
        nt->OptionalHeader.ImageBase == MPLAY_IMAGE_BASE &&
        nt->OptionalHeader.CheckSum == 0x5f297 &&
        (const uint8_t *)GetProcAddress(module, "?mplay_boot@@YAHW4E_MPLAY_MODE@@@Z") == base + 0x4470;
}

static bool mplay_patch(HMODULE module, uint8_t **installed)
{
    uint8_t *base = (uint8_t *)module;
    uint8_t windows[lengthof(mplay_sites)][sizeof(mplay_sites[0].bytes)];
    uint8_t replacement[lengthof(mplay_sites)][7];
    const size_t code_size = MPLAY_CODE_END - MPLAY_CODE_START;
    const size_t thunk_size = lengthof(mplay_sites) * MPLAY_THUNK_SIZE;
    uint8_t *code = base + MPLAY_CODE_START;
    MEMORY_BASIC_INFORMATION mapping;
    DWORD protection, ignored;

    if (!mplay_matches(module)) return false;
    for (size_t i = 0; i < lengthof(mplay_sites); i++) {
        const struct mplay_site *site = &mplay_sites[i];
        mplay_expected(site, base, windows[i]);
        if (*installed) {
            uint8_t *entry = base + site->rva + site->offset;
            uint8_t *thunk = *installed + i * MPLAY_THUNK_SIZE;
            memset(windows[i] + site->offset, 0x90, site->instruction_size);
            mplay_jump(windows[i] + site->offset, entry, thunk);
        }
        if (memcmp(base + site->rva, windows[i], site->size)) return false;
    }
    if (*installed) return true;

    /* Both pages must share one protection region so the group has one
       protection change and can be restored with one call. */
    if (!VirtualQuery(code, &mapping, sizeof(mapping)) ||
        mapping.AllocationBase != module || mapping.State != MEM_COMMIT ||
        mapping.Protect != PAGE_EXECUTE_READ ||
        (size_t)(code - (uint8_t *)mapping.BaseAddress) + code_size > mapping.RegionSize) {
        return false;
    }
    uint8_t *thunks = VirtualAlloc(NULL, thunk_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!thunks) return false;

    for (size_t i = 0; i < lengthof(mplay_sites); i++) {
        const struct mplay_site *site = &mplay_sites[i];
        uint8_t *entry = base + site->rva + site->offset;
        uint8_t *thunk = thunks + i * MPLAY_THUNK_SIZE;
        unsigned int size = site->instruction_size;
        /* LOCK ADD is the exact original ADD with atomic memory ordering.
           JMP leaves every register and flag unchanged, including SF for JNS. */
        thunk[0] = 0xf0;
        memcpy(thunk + 1, windows[i] + site->offset, size);
        mplay_jump(thunk + 1 + size, thunk + 1 + size, entry + size);
        memset(replacement[i], 0x90, size);
        mplay_jump(replacement[i], entry, thunk);
    }

    if (!VirtualProtect(thunks, thunk_size, PAGE_EXECUTE_READ, &ignored) ||
        !FlushInstructionCache(GetCurrentProcess(), thunks, thunk_size) ||
        !VirtualProtect(code, code_size, PAGE_EXECUTE_READWRITE, &protection)) {
        VirtualFree(thunks, 0, MEM_RELEASE);
        return false;
    }
    for (size_t i = 0; i < lengthof(mplay_sites); i++) {
        const struct mplay_site *site = &mplay_sites[i];
        memcpy(base + site->rva + site->offset, replacement[i], site->instruction_size);
    }
    if (!FlushInstructionCache(GetCurrentProcess(), code, code_size) ||
        !VirtualProtect(code, code_size, protection, &ignored)) {
        for (size_t i = 0; i < lengthof(mplay_sites); i++) {
            const struct mplay_site *site = &mplay_sites[i];
            memcpy(base + site->rva + site->offset, windows[i] + site->offset, site->instruction_size);
        }
        bool flushed = FlushInstructionCache(GetCurrentProcess(), code, code_size) != 0;
        bool restored = VirtualProtect(code, code_size, protection, &ignored) != 0;
        if (!flushed || !restored) {
            log_warning("GFDM V5 mplay: could not finish restoring original code pages");
        }
        /* Keep the executable thunks alive if the cache could not be synced. */
        if (flushed) VirtualFree(thunks, 0, MEM_RELEASE);
        return false;
    }
    *installed = thunks;
    return true;
}

void gfdm_mplay_apply_hooks(unsigned int version)
{
    static uint8_t *installed;
    if (version != 5) return;

    /* V5 boot.dll imports mplay_boot statically. Apply at the boot entry
       only, while no playback worker or queue producer can run. */
    HMODULE module = GetModuleHandleA("libshare-pj.dll");
    bool first = installed == NULL;
    if (!module || !mplay_patch(module, &installed)) {
        log_warning("GFDM V5 mplay: counter patch unavailable; using native code");
    } else if (first) {
        log_info("GFDM V5 mplay: atomic stop counter installed before native boot");
    }
}
