#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gfdmhook1/lan.h"
#include "util/defs.h"
#include "util/log.h"

/* Official IDA: V6 libshare-pj eam_network_settings_conflict rejects
   192.168/16 in DHCP mode. Skip only that comparison, retaining APIPA,
   missing gateway and missing netmask checks. libeamusement eam_boot_wait
   also replaces genuine EA3 errors with 2000(0) on that subnet; keep its
   original error branch instead. Apply before native boot, never as a live
   repair of an already latched error. No address/configuration is fabricated.
   These signatures include the branch destinations and use relocated VAs. */
static const uint8_t share_code[] = {
    0x3c, 0xc0, 0x75, 0x09, 0x80, 0x3d, 0xa9, 0x7d, 0x06, 0x10, 0xa8, 0xeb,
    0x0b, 0x3c, 0xa9, 0x75, 0x09, 0x80, 0x3d, 0xa9, 0x7d, 0x06, 0x10, 0xfe,
    0x74, 0x0e, 0x83, 0x7c, 0x24, 0x08, 0x00, 0x74, 0x07, 0x83, 0x7c, 0x24,
    0x0c, 0x00, 0x75, 0x08, 0x5e, 0xb0, 0x01, 0x5b, 0x83, 0xc4, 0x08, 0xc3,
    0x5e, 0x8a, 0xc3, 0x5b, 0x83, 0xc4, 0x08, 0xc3,
};
static const uint8_t eam_code[] = {
    0x0f, 0xb7, 0x0d, 0x2c, 0x19, 0x01, 0x10, 0x83, 0xc4, 0x0c, 0x81, 0xf9,
    0xc0, 0xa8, 0x00, 0x00, 0x75, 0x40, 0x8b, 0x15, 0x54, 0x17, 0x01, 0x10,
    0x6a, 0x02, 0x52, 0x68, 0x50, 0xc3, 0x00, 0x10, 0x68, 0x68, 0xc3, 0x00,
    0x10, 0xff, 0xd5, 0xc7, 0x05, 0x54, 0x17, 0x01, 0x10, 0x02, 0x00, 0x00,
    0x00, 0xc7, 0x05, 0x20, 0x19, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 0x89,
    0x5f, 0x08, 0xc7, 0x07, 0x05, 0x00, 0x00, 0x00, 0xc7, 0x47, 0x04, 0xd0,
    0x07, 0x00, 0x00, 0xbb, 0x30, 0xf8, 0xff, 0xff, 0xeb, 0x4e, 0x8b, 0x44,
    0x24, 0x10, 0x8b, 0x4c, 0x24, 0x14, 0x8b, 0xd8, 0xf7, 0xdb, 0x3d, 0xdc,
    0x07, 0x00, 0x00, 0xc7, 0x07, 0x05, 0x00, 0x00, 0x00, 0x89, 0x47, 0x04,
    0x89, 0x4f, 0x08, 0x74, 0x11, 0x83, 0xf9, 0x7f, 0x74, 0x0c,
};
static const uint8_t share_relocs[] = {6, 19};
static const uint8_t eam_relocs[] = {3, 20, 28, 33, 41, 51};

struct gfdm_lan_patch {
    const char *module;
    const char *symbol;
    DWORD timestamp, image_size, export_rva, code_rva;
    const uint8_t *code, *relocs;
    size_t size, nrelocs, opcode;
};

static const struct gfdm_lan_patch lan_patches[] = {
    {"libshare-pj.dll",
     "eam_network_settings_conflict",
     0x4a275061,
     0x16f000,
     0x18e20,
     0x18f10,
     share_code,
     share_relocs,
     sizeof(share_code),
     sizeof(share_relocs),
     2},
    {"libeamusement.dll",
     "eam_boot_wait",
     0x4a275056,
     0x1c000,
     0x2b70,
     0x2bdb,
     eam_code,
     eam_relocs,
     sizeof(eam_code),
     sizeof(eam_relocs),
     16},
};

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
        log_warning("GFDM V6 LAN: could not restore code page protection");
        return false;
    }
    log_info(
        "GFDM V6 LAN: patched %s!%s for private LAN support",
        patch->module,
        patch->symbol);
    return true;
}

void gfdm_lan_apply_hooks(unsigned int version)
{
    static volatile LONG applying;
    static volatile LONG pending;

    if (version != 6) {
        return;
    }
    /* Never wait recursively under the loader lock. If another callback
       loads a module during this scan, the active caller rescans afterward.
       Serialize page protection changes, including repeated applications. */
    InterlockedExchange(&pending, 1);
    do {
        if (InterlockedCompareExchange(&applying, 1, 0) != 0) {
            return;
        }
        InterlockedExchange(&pending, 0);
        for (size_t i = 0; i < lengthof(lan_patches); i++) {
            HMODULE module = GetModuleHandleA(lan_patches[i].module);
            if (module != NULL &&
                !gfdm_lan_patch_module(module, &lan_patches[i])) {
                log_warning(
                    "GFDM V6 LAN: unsupported or unpatchable %s; "
                    "native behavior retained where not patched",
                    lan_patches[i].module);
            }
        }
        InterlockedExchange(&applying, 0);
    } while (InterlockedCompareExchange(&pending, 0, 0) != 0);
}
