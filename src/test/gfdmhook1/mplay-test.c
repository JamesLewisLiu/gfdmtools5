#undef NDEBUG
#define LOG_SUPPRESS
#include <windows.h>
#include <assert.h>
#include <stdio.h>

static FARPROC WINAPI fixture_export(HMODULE module, LPCSTR name);
static BOOL WINAPI fixture_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old);
static BOOL WINAPI fixture_flush(HANDLE process, LPCVOID address, SIZE_T size);
#define GetProcAddress fixture_export
#define VirtualProtect fixture_protect
#define FlushInstructionCache fixture_flush
#include "gfdmhook1/mplay.c"
#undef GetProcAddress
#undef VirtualProtect
#undef FlushInstructionCache

static bool wrong_export, fail_protect, fail_flush, fail_restore;
static uint8_t *image;

static FARPROC WINAPI fixture_export(HMODULE module, LPCSTR name)
{
    assert((void *)module == image);
    assert(!strcmp(name, "?mplay_boot@@YAHW4E_MPLAY_MODE@@@Z"));
    return (FARPROC)(void *)(image + (wrong_export ? 0x4471 : 0x4470));
}

static BOOL WINAPI fixture_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old)
{
    if (address == image + MPLAY_CODE_START) {
        if (fail_protect && protection == PAGE_EXECUTE_READWRITE) {
            fail_protect = false;
            return FALSE;
        }
        if (fail_restore && protection == PAGE_EXECUTE_READ) {
            fail_restore = false;
            return FALSE;
        }
    }
    return VirtualProtect(address, size, protection, old);
}

static BOOL WINAPI fixture_flush(HANDLE process, LPCVOID address, SIZE_T size)
{
    if (fail_flush && address == image + MPLAY_CODE_START) {
        fail_flush = false;
        return FALSE;
    }
    return FlushInstructionCache(process, address, size);
}

static void rejected_unchanged(void)
{
    uint8_t before[MPLAY_CODE_END - MPLAY_CODE_START];
    uint8_t *installed = NULL;
    MEMORY_BASIC_INFORMATION map;
    memcpy(before, image + MPLAY_CODE_START, sizeof(before));
    assert(!mplay_patch((HMODULE)image, &installed));
    assert(installed == NULL);
    assert(!memcmp(before, image + MPLAY_CODE_START, sizeof(before)));
    assert(VirtualQuery(image + MPLAY_CODE_START, &map, sizeof(map)));
    assert(map.Protect == PAGE_EXECUTE_READ);
}

typedef unsigned int (__cdecl *update_fn)(void);
static update_fn enqueue, stop_bgm, stop_movie;
static HANDLE ready;
enum { SIGN_FLAG = 0x80, CARRY_FLAG = 1, REQUESTS = 200000 };

static DWORD WINAPI consume(void *unused)
{
    (void)unused;
    for (unsigned int n = 0; n < REQUESTS; n++) {
        assert(WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0);
        assert(!(stop_bgm() & SIGN_FLAG));
    }
    return 0;
}

static void exercise_machine_code(void)
{
    DWORD ignored;
    volatile LONG *counter = (LONG *)(image + 0x49f60);
    uint8_t *driver = image + 0x5800;
    assert(VirtualProtect(image + 0x4000, 0x2000, PAGE_EXECUTE_READWRITE, &ignored));
    /* Preserve each detour and record ADD flags at its continuation. */
    for (size_t n = 0; n < lengthof(mplay_sites); n++) {
        const struct mplay_site *s = &mplay_sites[n];
        memcpy(image + s->rva + s->offset + s->instruction_size, "\x9c\x58\xc3", 3);
    }
    memcpy(driver, "\xb8\x01\x00\x00\x00", 5);
    mplay_jump(driver + 5, driver + 5, image + 0x5793);
    assert(VirtualProtect(image + 0x4000, 0x2000, PAGE_EXECUTE_READ, &ignored));
    assert(FlushInstructionCache(GetCurrentProcess(), image + 0x4000, 0x2000));
    enqueue = (update_fn)(void *)driver;
    stop_bgm = (update_fn)(void *)(image + 0x4fe4);
    stop_movie = (update_fn)(void *)(image + 0x5130);

    assert((stop_bgm() & (SIGN_FLAG | CARRY_FLAG)) == SIGN_FLAG && *counter == -1);
    assert((enqueue() & (SIGN_FLAG | CARRY_FLAG)) == CARRY_FLAG && *counter == 0);
    enqueue(); enqueue();
    assert(*counter == 2);
    assert((stop_bgm() & (SIGN_FLAG | CARRY_FLAG)) == CARRY_FLAG && *counter == 1);
    assert((stop_bgm() & (SIGN_FLAG | CARRY_FLAG)) == CARRY_FLAG && *counter == 0);
    enqueue();
    assert((stop_movie() & (SIGN_FLAG | CARRY_FLAG)) == CARRY_FLAG && *counter == 0);
    assert((stop_movie() & SIGN_FLAG) && *counter == -1);
    *counter = 0;

    ready = CreateSemaphoreA(NULL, 0, REQUESTS, NULL);
    assert(ready);
    HANDLE consumer = CreateThread(NULL, 0, consume, NULL, 0, NULL);
    assert(consumer);
    for (unsigned int n = 0; n < REQUESTS; n++) {
        enqueue();
        assert(ReleaseSemaphore(ready, 1, NULL));
    }
    assert(WaitForSingleObject(consumer, 20000) == WAIT_OBJECT_0);
    assert(*counter == 0);
    CloseHandle(consumer);
    CloseHandle(ready);
}

int main(void)
{
    DWORD ignored;
    uint8_t *installed = NULL;
    image = VirtualAlloc(NULL, MPLAY_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(image && (uintptr_t)image != MPLAY_IMAGE_BASE);
    IMAGE_DOS_HEADER *dos = (void *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (void *)(image + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.TimeDateStamp = 0x491aba19;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfImage = MPLAY_IMAGE_SIZE;
    nt->OptionalHeader.ImageBase = MPLAY_IMAGE_BASE;
    nt->OptionalHeader.CheckSum = 0x5f297;
    for (size_t n = 0; n < lengthof(mplay_sites); n++)
        mplay_expected(&mplay_sites[n], image, image + mplay_sites[n].rva);
    assert(VirtualProtect(image + 0x4000, 0x2000, PAGE_EXECUTE_READ, &ignored));
    nt->FileHeader.TimeDateStamp++;
    rejected_unchanged();
    nt->FileHeader.TimeDateStamp--;
    wrong_export = true;
    rejected_unchanged();
    wrong_export = false;

    assert(VirtualProtect(image + 0x5000, 0x1000, PAGE_EXECUTE_READWRITE, &ignored));
    image[0x5793] = 0xe9;
    assert(VirtualProtect(image + 0x5000, 0x1000, PAGE_EXECUTE_READ, &ignored));
    rejected_unchanged();
    assert(VirtualProtect(image + 0x5000, 0x1000, PAGE_EXECUTE_READWRITE, &ignored));
    image[0x5793] = 0x01;
    assert(VirtualProtect(image + 0x5000, 0x1000, PAGE_EXECUTE_READ, &ignored));
    fail_protect = true; rejected_unchanged(); assert(!fail_protect);
    fail_flush = true; rejected_unchanged(); assert(!fail_flush);
    fail_restore = true; rejected_unchanged(); assert(!fail_restore);

    assert(mplay_patch((HMODULE)image, &installed) && installed);
    uint8_t *same = installed;
    assert(mplay_patch((HMODULE)image, &installed) && installed == same);
    assert(image[0x4ff1] == 0x79 && image[0x4ff2] == 0x18);
    assert(!memcmp(image + 0x4ff8, "\x68\x80\x05\x00\x00", 5));
    assert(image[0x5791] == 0x75 && image[0x5792] == 6 && image[0x5799] == 0x33);
    exercise_machine_code();
    assert(VirtualFree(installed, 0, MEM_RELEASE));
    assert(VirtualFree(image, 0, MEM_RELEASE));
    puts("PASS: V5 mplay guards, patch rollback, relocated x86 code, flags and concurrent stop accounting");
    return 0;
}
