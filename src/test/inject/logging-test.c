#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "inject/options.c"

static void check_options(void)
{
    struct options options;
    char *base[] = {"inject.exe", "hook.dll", "game.exe"};
    char *bare[] = {"inject.exe", "hook.dll", "game.exe", "-Y"};
    char *named[] = {"inject.exe", "hook.dll", "game.exe", "-Y", "my log.txt"};
    char *switches[] = {
        "inject.exe",
        "hook.dll",
        "game.exe",
        "-Y",
        "-d",
        "--config",
        "dm.conf"};
    char *similar[] = {
        "inject.exe", "hook.dll", "game.exe", "-Yellow", "-Debug", "-Remote"};
    char *repeat[] = {
        "inject.exe", "hook.dll", "game.exe", "-Y", "old.log", "-Y"};
    char *empty[] = {"inject.exe", "hook.dll", "game.exe", "-Y", ""};
    char long_name[MAX_PATH + 1];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    char *too_long[] = {"inject.exe", "hook.dll", "game.exe", "-Y", long_name};
#define PARSE(args)          \
    (options_init(&options), \
     options_read_cmdline(&options, sizeof(args) / sizeof(args[0]), args))
    assert(PARSE(base) && !options.log_file[0]);
    assert(PARSE(bare) && !strcmp(options.log_file, "log.log"));
    assert(PARSE(named) && !strcmp(options.log_file, "my log.txt"));
    assert(PARSE(switches) && !strcmp(options.log_file, "log.log"));
    assert(
        PARSE(similar) && !options.log_file[0] && !options.debug &&
        !options.remote_debugger);
    assert(PARSE(repeat) && !strcmp(options.log_file, "log.log"));
    assert(!PARSE(empty));
    assert(!PARSE(too_long));
#undef PARSE
}

static void run_process(const char *application, char *command, DWORD flags)
{
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process;
    DWORD code;
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    assert(CreateProcessA(
        application,
        command,
        NULL,
        NULL,
        FALSE,
        flags,
        NULL,
        NULL,
        &startup,
        &process));
    assert(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0);
    assert(GetExitCodeProcess(process.hProcess, &code));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    assert(code == 0);
}

static char *read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *text = calloc(size + 1, 1);
    assert(text && fread(text, 1, size, file) == (size_t) size);
    fclose(file);
    return text;
}

static void exactly_once(const char *text, const char *marker)
{
    const char *match = strstr(text, marker);
    assert(match && !strstr(match + strlen(marker), marker));
}

static void check_markers(const char *text)
{
    exactly_once(text, "AVS-RAW");
    exactly_once(text, "I:fixture: dll-attach");
    exactly_once(text, "I:fixture: application-complete");
    for (unsigned thread = 0; thread < 4; thread++) {
        for (unsigned i = 0; i < 20; i++) {
            char marker[80];
            /* Include a terminator to distinguish worker-0-1 from worker-0-10.
             */
            snprintf(
                marker, sizeof(marker), "I:fixture: worker-%u-%u\n", thread, i);
            exactly_once(text, marker);
        }
    }
}

static void console_host(const char *directory)
{
    char inject[MAX_PATH], hook[MAX_PATH], game[MAX_PATH], command[4096];
    snprintf(inject, sizeof(inject), "%s\\inject.exe", directory);
    snprintf(hook, sizeof(hook), "%s\\gfdm-log-hook.dll", directory);
    snprintf(game, sizeof(game), "%s\\gfdm-log-game.exe", directory);
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    assert(SetConsoleScreenBufferSize(console, (COORD) {200, 2000}));
    const char *args[] = {
        "",
        "-Y",
        "-Y \"custom log.txt\"",
        "-Y -d --config dm.conf",
        "-D",
        "-D -Y",
        "-D -Y \"custom log.txt\""};
    const char *files[] = {
        NULL,
        "log.log",
        "custom log.txt",
        "log.log",
        NULL,
        "log.log",
        "custom log.txt"};
    for (unsigned i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
        DWORD count;
        COORD origin = {0, 0};
        assert(
            FillConsoleOutputCharacterA(console, ' ', 400000, origin, &count));
        assert(SetConsoleCursorPosition(console, origin));
        DeleteFileA("log.log");
        DeleteFileA("custom log.txt");
        if (files[i]) {
            FILE *stale = fopen(files[i], "wb");
            assert(stale);
            fputs("STALE-LOG-CONTENT", stale);
            fclose(stale);
        }
        snprintf(
            command,
            sizeof(command),
            "\"%s\" \"%s\" \"%s\" %s",
            inject,
            hook,
            game,
            args[i]);
        run_process(inject, command, 0);
        char *screen = calloc(400001, 1);
        assert(
            screen &&
            ReadConsoleOutputCharacterA(
                console, screen, 400000, origin, &count));
        /* Console cells have spaces at line ends rather than newline
         * characters. */
        exactly_once(screen, "I:fixture: dll-attach");
        exactly_once(screen, "I:fixture: application-complete");
        exactly_once(screen, "I:fixture: worker-3-19");
        free(screen);
        if (files[i]) {
            char *text = read_text(files[i]);
            assert(!strstr(text, "STALE-LOG-CONTENT"));
            assert(strstr(text, "Inject build date"));
            check_markers(text);
            free(text);
        } else {
            assert(GetFileAttributesA("log.log") == INVALID_FILE_ATTRIBUTES);
        }
        assert(
            GetFileAttributesA("gfdm-v4-hook.log") == INVALID_FILE_ATTRIBUTES);
        assert(
            GetFileAttributesA("gfdm-v8-hook.log") == INVALID_FILE_ATTRIBUTES);
    }
    DeleteFileA("log.log");
    DeleteFileA("custom log.txt");
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    check_options();
    if (argc == 3 && !strcmp(argv[1], "--console-host")) {
        console_host(argv[2]);
        return 0;
    }
    char executable[MAX_PATH], directory[MAX_PATH], temporary[MAX_PATH],
        work[MAX_PATH];
    char command[4096];
    assert(GetModuleFileNameA(NULL, executable, sizeof(executable)));
    strcpy(directory, executable);
    char *separator = strrchr(directory, '\\');
    assert(separator);
    *separator = 0;
    assert(GetTempPathA(sizeof(temporary), temporary));
    assert(GetTempFileNameA(temporary, "gfl", 0, work));
    assert(DeleteFileA(work) && CreateDirectoryA(work, NULL));
    assert(SetCurrentDirectoryA(work));
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --console-host \"%s\"",
        executable,
        directory);
    run_process(executable, command, CREATE_NEW_CONSOLE);
    assert(SetCurrentDirectoryA(temporary) && RemoveDirectoryA(work));
    puts(
        "GFDM logging: options and seven real-inject GUI/console/file cases "
        "passed");
    return 0;
}
