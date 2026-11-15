#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HMODULE test_module(const char *name);
static void *test_export(HMODULE module, const char *name);
#define GetModuleHandleA test_module
#define GetProcAddress test_export
#include "gfdmhook1/lan.c"
#undef GetModuleHandleA
#undef GetProcAddress

static HMODULE modules[2];
static bool visible[2], wrong_export, real_exports, reenter_scan;

static HMODULE test_module(const char *name)
{
    for (size_t i = 0; i < lengthof(lan_patches); i++) {
        if (!strcmp(name, lan_patches[i].module)) {
            if (i == 1 && reenter_scan) {
                reenter_scan = false;
                visible[0] = true;
                gfdm_lan_apply_hooks(6);
            }
            return visible[i] ? modules[i] : NULL;
        }
    }
    assert(false);
    return NULL;
}

static void *test_export(HMODULE module, const char *name)
{
    if (real_exports)
        return (void *) GetProcAddress(module, name);
    for (size_t i = 0; i < lengthof(lan_patches); i++) {
        if (modules[i] == module) {
            assert(!strcmp(name, lan_patches[i].symbol));
            return (uint8_t *) module + lan_patches[i].export_rva +
                (wrong_export ? 1 : 0);
        }
    }
    assert(false);
    return NULL;
}

static IMAGE_NT_HEADERS32 *headers(size_t i)
{
    return (void *) ((uint8_t *) modules[i] + 0x80);
}

static uint8_t *code(size_t i)
{
    return (uint8_t *) modules[i] + lan_patches[i].code_rva;
}

static void initialize(size_t i)
{
    const struct gfdm_lan_patch *p = &lan_patches[i];
    IMAGE_DOS_HEADER *dos = (void *) modules[i];
    IMAGE_NT_HEADERS32 *nt = headers(i);
    uint32_t address;
    DWORD old;

    assert(VirtualProtect(modules[i], p->image_size, PAGE_READWRITE, &old));
    memset(modules[i], 0, p->image_size);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.TimeDateStamp = p->timestamp;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfImage = p->image_size;
    memcpy(code(i), p->code, p->size);
    for (size_t r = 0; r < p->nrelocs; r++) {
        memcpy(&address, code(i) + p->relocs[r], sizeof(address));
        address += (uint32_t) (uintptr_t) modules[i] - 0x10000000u;
        memcpy(code(i) + p->relocs[r], &address, sizeof(address));
    }
}

static void rejected_unchanged(size_t i)
{
    const struct gfdm_lan_patch *p = &lan_patches[i];
    uint8_t *before = malloc(p->image_size);
    assert(before != NULL);
    memcpy(before, modules[i], p->image_size);
    assert(!gfdm_lan_patch_module(modules[i], p));
    assert(!memcmp(before, modules[i], p->image_size));
    free(before);
}

static void test_guards(size_t i)
{
    const struct gfdm_lan_patch *p = &lan_patches[i];
    IMAGE_NT_HEADERS32 *nt = headers(i);
    IMAGE_DOS_HEADER *dos = (void *) modules[i];

    initialize(i);
    nt->FileHeader.TimeDateStamp ^= 1;
    rejected_unchanged(i);
    initialize(i);
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    rejected_unchanged(i);
    initialize(i);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    rejected_unchanged(i);
    initialize(i);
    nt->OptionalHeader.SizeOfImage -= 0x1000;
    rejected_unchanged(i);
    initialize(i);
    nt->Signature ^= 1;
    rejected_unchanged(i);
    initialize(i);
    dos->e_magic ^= 1;
    rejected_unchanged(i);
    initialize(i);
    dos->e_lfanew = -1;
    rejected_unchanged(i);
    initialize(i);
    dos->e_lfanew = 0x1000;
    rejected_unchanged(i);
    initialize(i);
    wrong_export = true;
    rejected_unchanged(i);
    wrong_export = false;

    /* Every code byte, including relocated operands and destinations, is
       guarded. Also reject a mismatched window with an already-JMP opcode. */
    for (size_t b = 0; b < p->size; b++) {
        initialize(i);
        code(i)[b] ^= 1;
        rejected_unchanged(i);
        if (b != p->opcode) {
            code(i)[p->opcode] = 0xeb;
            rejected_unchanged(i);
        }
    }
}

static void test_patch(size_t i)
{
    const struct gfdm_lan_patch *p = &lan_patches[i];
    uint8_t *expected = malloc(p->image_size);
    MEMORY_BASIC_INFORMATION info;
    DWORD old;

    assert(expected != NULL);
    initialize(i);
    memcpy(expected, modules[i], p->image_size);
    expected[p->code_rva + p->opcode] = 0xeb;
    assert(VirtualProtect(code(i), p->size, PAGE_EXECUTE_READ, &old));
    assert(gfdm_lan_patch_module(modules[i], p));
    assert(!memcmp(expected, modules[i], p->image_size));
    assert(VirtualQuery(code(i), &info, sizeof(info)) == sizeof(info));
    assert(info.Protect == PAGE_EXECUTE_READ);
    assert(gfdm_lan_patch_module(modules[i], p));
    assert(!memcmp(expected, modules[i], p->image_size));
    assert(VirtualQuery(code(i), &info, sizeof(info)) == sizeof(info));
    assert(info.Protect == PAGE_EXECUTE_READ);
    free(expected);
}

static void test_lifecycle(void)
{
    initialize(0);
    initialize(1);
    visible[0] = visible[1] = true;
    for (unsigned int v = 0; v <= 9; v++) {
        if (v != 6)
            gfdm_lan_apply_hooks(v);
    }
    assert(code(0)[lan_patches[0].opcode] == 0x75);
    assert(code(1)[lan_patches[1].opcode] == 0x75);
    visible[0] = visible[1] = false;
    gfdm_lan_apply_hooks(6);
    assert(code(0)[lan_patches[0].opcode] == 0x75);
    assert(code(1)[lan_patches[1].opcode] == 0x75);
    visible[0] = true;
    gfdm_lan_apply_hooks(6);
    assert(code(0)[lan_patches[0].opcode] == 0xeb);
    assert(code(1)[lan_patches[1].opcode] == 0x75);
    visible[1] = true;
    gfdm_lan_apply_hooks(6);
    assert(code(0)[lan_patches[0].opcode] == 0xeb);
    assert(code(1)[lan_patches[1].opcode] == 0xeb);
    /* A fresh mapping at the same address must be checked again. */
    initialize(0);
    gfdm_lan_apply_hooks(6);
    assert(code(0)[lan_patches[0].opcode] == 0xeb);
    initialize(0);
    initialize(1);
    visible[0] = false;
    visible[1] = true;
    reenter_scan = true;
    gfdm_lan_apply_hooks(6);
    assert(!reenter_scan);
    assert(code(0)[lan_patches[0].opcode] == 0xeb);
    assert(code(1)[lan_patches[1].opcode] == 0xeb);
}

static DWORD WINAPI concurrent_scan(void *event)
{
    assert(WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 500; i++)
        gfdm_lan_apply_hooks(6);
    return 0;
}

static void test_concurrent(void)
{
    HANDLE threads[4];
    HANDLE event = CreateEventA(NULL, TRUE, FALSE, NULL);
    uint8_t *expected[2];
    DWORD old;
    MEMORY_BASIC_INFORMATION info;
    assert(event != NULL);
    for (size_t i = 0; i < lengthof(lan_patches); i++) {
        const struct gfdm_lan_patch *p = &lan_patches[i];
        initialize(i);
        visible[i] = true;
        expected[i] = malloc(p->image_size);
        assert(expected[i] != NULL);
        memcpy(expected[i], modules[i], p->image_size);
        expected[i][p->code_rva + p->opcode] = 0xeb;
        assert(VirtualProtect(code(i), p->size, PAGE_EXECUTE_READ, &old));
    }
    for (size_t i = 0; i < lengthof(threads); i++) {
        threads[i] = CreateThread(NULL, 0, concurrent_scan, event, 0, NULL);
        assert(threads[i] != NULL);
    }
    assert(SetEvent(event));
    assert(
        WaitForMultipleObjects(lengthof(threads), threads, TRUE, 30000) ==
        WAIT_OBJECT_0);
    for (size_t i = 0; i < lengthof(threads); i++)
        assert(CloseHandle(threads[i]));
    assert(CloseHandle(event));
    for (size_t i = 0; i < lengthof(lan_patches); i++) {
        assert(!memcmp(expected[i], modules[i], lan_patches[i].image_size));
        assert(VirtualQuery(code(i), &info, sizeof(info)) == sizeof(info));
        assert(info.Protect == PAGE_EXECUTE_READ);
        free(expected[i]);
    }
}

static void test_native(const char *share, const char *eam)
{
    const char *paths[] = {share, eam};
    /* Force the actual Windows PE loader to relocate both original images. */
    void *preferred = VirtualAlloc(
        (void *) (uintptr_t) 0x10000000u, 0x200000, MEM_RESERVE, PAGE_NOACCESS);
    assert(preferred == (void *) (uintptr_t) 0x10000000u);
    real_exports = true;
    for (size_t i = 0; i < lengthof(lan_patches); i++) {
        const struct gfdm_lan_patch *p = &lan_patches[i];
        uint8_t expected[128];
        MEMORY_BASIC_INFORMATION before, after;
        HMODULE module =
            LoadLibraryExA(paths[i], NULL, DONT_RESOLVE_DLL_REFERENCES);
        assert(module != NULL); /* PE32 test binary required for native PE32. */
        assert((uintptr_t) module != 0x10000000u);
        uint8_t *window = (uint8_t *) module + p->code_rva;
        memcpy(expected, window, p->size);
        assert(expected[p->opcode] == 0x75);
        expected[p->opcode] = 0xeb;
        assert(VirtualQuery(window, &before, sizeof(before)) == sizeof(before));
        assert(gfdm_lan_patch_module(module, p));
        assert(!memcmp(expected, window, p->size));
        assert(gfdm_lan_patch_module(module, p));
        assert(VirtualQuery(window, &after, sizeof(after)) == sizeof(after));
        assert(before.Protect == after.Protect);
        printf(
            "PASS relocated native PE32 patch "
            "signature/protection/idempotence: %s\n",
            p->module);
        assert(FreeLibrary(module));
    }
    assert(VirtualFree(preferred, 0, MEM_RELEASE));
    real_exports = false;
}

int main(int argc, char **argv)
{
    assert(argc == 1 || argc == 3);
    for (size_t i = 0; i < lengthof(lan_patches); i++) {
        modules[i] = VirtualAlloc(
            NULL,
            lan_patches[i].image_size,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE);
        assert(modules[i] != NULL);
        assert((uintptr_t) modules[i] != 0x10000000u);
        test_guards(i);
        test_patch(i);
    }
    test_lifecycle();
    test_concurrent();
    for (size_t i = 0; i < lengthof(lan_patches); i++)
        assert(VirtualFree(modules[i], 0, MEM_RELEASE));
    puts(
        "PASS V6 LAN PE guards, relocated signatures, exact writes, "
        "protection, reentry and concurrency");
    if (argc == 3)
        test_native(argv[1], argv[2]);
    return 0;
}
