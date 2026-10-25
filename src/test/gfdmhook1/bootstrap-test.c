#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "gfdmhook1/bootstrap.c"
#include "gfdmhook1/profile.c"

static void read_virtual(const char *path, char *data, size_t count)
{
    FILE *file = fopen(path, "rb");
    size_t size;
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && (size_t) length < count);
    assert(fseek(file, 0, SEEK_SET) == 0);
    size = fread(data, 1, count - 1, file);
    assert(size == (size_t) length && !ferror(file));
    data[size] = '\0';
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    char executable[MAX_PATH], command[MAX_PATH + 32], data[2048], expected[64];
    if (argc == 1) {
        assert(GetModuleFileNameA(NULL, executable, sizeof(executable)) != 0);
        for (unsigned int version = 4; version <= 8; version++) {
            for (unsigned int guitar = 0; guitar <= 1; guitar++) {
                STARTUPINFOA startup = {.cb = sizeof(startup)};
                PROCESS_INFORMATION process;
                DWORD code;
                char temp[MAX_PATH];
                assert(GetTempPathA(sizeof(temp), temp) != 0);
                snprintf(
                    command,
                    sizeof(command),
                    "\"%s\" %u %u",
                    executable,
                    version,
                    guitar);
                assert(CreateProcessA(
                    NULL,
                    command,
                    NULL,
                    NULL,
                    FALSE,
                    0,
                    NULL,
                    temp,
                    &startup,
                    &process));
                assert(
                    WaitForSingleObject(process.hProcess, 10000) ==
                    WAIT_OBJECT_0);
                assert(
                    GetExitCodeProcess(process.hProcess, &code) && code == 0);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }
        puts(
            "PASS: ten version/mode identities, native CRT "
            "reads/seeks/reopens, built-in AVS settings and absent disk XML");
        return 0;
    }
    assert(argc == 3);
    unsigned int version = (unsigned int) atoi(argv[1]);
    bool guitar = atoi(argv[2]) != 0;
    snprintf(executable, sizeof(executable), "gdv%u.exe", version);
    assert(gfdm_bootstrap_init(gfdm_profile_find(executable), guitar));
    assert(
        GetFileAttributesA("C:\\no-such-gfdm-directory\\gfdm-boot.xml") ==
        INVALID_FILE_ATTRIBUTES);
    read_virtual(
        "C:\\no-such-gfdm-directory\\gfdm-boot.xml", data, sizeof(data));
    assert(strstr(data, "<enable_win32fs __type=\"bool\">1</enable_win32fs>"));
    assert(strstr(data, "<case_sensitive __type=\"bool\">0</case_sensitive>"));
    assert(strstr(data, "<autoconf __type=\"bool\">1</autoconf>"));
    assert(strstr(data, "<enable_ntp __type=\"bool\">1</enable_ntp>"));
    assert(strstr(
        data,
        "<services "
        "__type=\"str\">https://eamuse.konami.fun/service/services/</"
        "services>"));
    read_virtual("gfdm-ea3-ident.xml", data, sizeof(data));
    snprintf(
        expected,
        sizeof(expected),
        "<model __type=\"str\">%c%d</model>",
        'G' + version - 4,
        guitar ? 33 : 32);
    assert(strstr(data, expected));
    snprintf(
        expected,
        sizeof(expected),
        "<spec __type=\"str\">%c</spec>",
        version >= 7 ? 'B' : 'A');
    assert(strstr(data, expected));
    assert(strstr(data, "<dest __type=\"str\">J</dest>"));
    assert(strstr(data, "<rev __type=\"str\">A</rev>"));
    read_virtual("gfdm-ea3-ident.xml", data, sizeof(data));
    return 0;
}
