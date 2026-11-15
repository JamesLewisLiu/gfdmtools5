#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gfdmhook1/lan.h"
#include "util/defs.h"
#include "util/log.h"

/* Official IDA: V4-V8 libshare-pj eam_network_settings_conflict rejects
   192.168/16 in DHCP mode. Skip only that comparison, retaining APIPA,
   missing gateway and missing netmask checks where the native version has them.
   V4 has no separate APIPA check. libeamusement eam_boot_wait
   also replaces genuine EA3 errors with 2000(0) on that subnet; keep its
   original error branch instead. Apply before native boot, never as a live
   repair of an already latched error. No address/configuration is fabricated.
   These signatures include the branch destinations and use relocated VAs. */
struct gfdm_lan_patch {
    const char *module;
    const char *symbol;
    DWORD timestamp, image_size, export_rva, code_rva;
    const uint8_t *code, *relocs;
    size_t size, nrelocs, opcode;
};

#include "gfdmhook1/lan-data.h"

static bool
gfdm_lan_patch_module(HMODULE module, const struct gfdm_lan_patch *patch)
{
    uint8_t expected[128];
    uint8_t *base = (uint8_t *) module;
    IMAGE_DOS_HEADER *dos = (void *) base;
    IMAGE_NT_HEADERS32 *nt;
    DWORD protection, ignored;
    uint32_t address;
    uint8_t *code;

    /* Only loaded PE32 images with the verified export and local code are
       admitted. A different revision or another patch leaves native code alone.
     */
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 0x1000 - (LONG) sizeof(*nt)) {
        return false;
    }
    nt = (void *) (base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->FileHeader.TimeDateStamp != patch->timestamp ||
        nt->OptionalHeader.SizeOfImage != patch->image_size ||
        (uint8_t *) GetProcAddress(module, patch->symbol) !=
            base + patch->export_rva) {
        return false;
    }
    memcpy(expected, patch->code, patch->size);
    for (size_t i = 0; i < patch->nrelocs; i++) {
        memcpy(&address, expected + patch->relocs[i], sizeof(address));
        address += (uint32_t) (uintptr_t) base - 0x10000000u;
        memcpy(expected + patch->relocs[i], &address, sizeof(address));
    }
    code = base + patch->code_rva;
    if (code[patch->opcode] == 0xeb) {
        expected[patch->opcode] = 0xeb;
        return memcmp(code, expected, patch->size) == 0;
    }
    if (memcmp(code, expected, patch->size) != 0 ||
        !VirtualProtect(
            code + patch->opcode, 1, PAGE_EXECUTE_READWRITE, &protection)) {
        return false;
    }
    code[patch->opcode] = 0xeb; /* JNZ -> JMP, same signed displacement. */
    if (!FlushInstructionCache(GetCurrentProcess(), code + patch->opcode, 1)) {
        code[patch->opcode] = 0x75;
        FlushInstructionCache(GetCurrentProcess(), code + patch->opcode, 1);
        VirtualProtect(code + patch->opcode, 1, protection, &ignored);
        return false;
    }
    if (!VirtualProtect(code + patch->opcode, 1, protection, &ignored)) {
        log_warning("GFDM LAN: could not restore code page protection");
        return false;
    }
    log_info(
        "GFDM LAN: patched %s!%s for private LAN support",
        patch->module,
        patch->symbol);
    return true;
}

void gfdm_lan_apply_hooks(unsigned int version)
{
    static volatile LONG applying;
    static volatile LONG pending;
    const struct gfdm_lan_patch *lan_patches;

    if (version < 4 || version > 8) {
        return;
    }
    lan_patches = lan_profiles[version - 4];
    /* Never wait recursively under the loader lock. If another callback
       loads a module during this scan, the active caller rescans afterward.
       Serialize page protection changes, including repeated applications. */
    InterlockedExchange(&pending, 1);
    do {
        if (InterlockedCompareExchange(&applying, 1, 0) != 0) {
            return;
        }
        InterlockedExchange(&pending, 0);
        for (size_t i = 0; i < 2; i++) {
            HMODULE module = GetModuleHandleA(lan_patches[i].module);
            if (module != NULL &&
                !gfdm_lan_patch_module(module, &lan_patches[i])) {
                log_warning(
                    "GFDM LAN: unsupported or unpatchable %s; "
                    "native behavior retained where not patched",
                    lan_patches[i].module);
            }
        }
        InterlockedExchange(&applying, 0);
    } while (InterlockedCompareExchange(&pending, 0, 0) != 0);
}
