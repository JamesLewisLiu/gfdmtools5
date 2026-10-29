#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inject/options.h"

#include "util/defs.h"
#include "util/log-file.h"

void options_init(struct options *options)
{
    options->debug = false;
    options->remote_debugger = false;
    memset(options->log_file, 0, sizeof(options->log_file));
}

bool options_read_cmdline(struct options *options, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'D':
                    if (!strcmp(argv[i], "-D")) {
                        options->debug = true;
                    }

                    break;

                case 'Y':
                    if (strcmp(argv[i], "-Y")) {
                        break;
                    }
                    if (!log_file_option(argc, argv, &i, options->log_file,
                                         sizeof(options->log_file))) {
                        return false;
                    }
                    break;

                case 'R':
                    if (!strcmp(argv[i], "-R")) {
                        options->remote_debugger = true;
                    }

                    break;

                default:
                    break;
            }
        }
    }

    return true;
}

void options_print_usage(void)
{
    fprintf(stderr,
        "inject build " __DATE__ " " __TIME__
        ", gitrev " STRINGIFY(GITREV) "\n"
        "Usage: inject hook.dll... app.exe [hooks options...]\n"
        "You can specify one or multiple hook.dll files, e.g. inject.exe "
        "hook1.dll hook2.dll app.exe"
        "\n"
        "An IAT replacement early injection can be specified using the syntax orig.dll=hook.dll"
        "\n"
        "       The following options can be specified after the exe path:\n"
        "\n"
        "       -D Enable debugging output\n"
        "       -R Halt the injected process until a debugger is attached\n"
        "       -Y [filename]   Also write a file (default: log.log)\n");
}
