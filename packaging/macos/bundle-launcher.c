#include <mach-o/dyld.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef IDP_LAUNCH_PROFILE
#error IDP_LAUNCH_PROFILE must be 0 (MCP), 1 (Partner), or 2 (Partner G)
#endif
#if IDP_LAUNCH_PROFILE < 0 || IDP_LAUNCH_PROFILE > 2
#error IDP_LAUNCH_PROFILE must be 0 (MCP), 1 (Partner), or 2 (Partner G)
#endif

int main(int argc, char **argv)
{
    char executable[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) != 0) {
        fprintf(stderr, "Partner launcher path is too long\n");
        return 127;
    }

    char applications[PATH_MAX];
    if (!realpath(executable, applications)) {
        fprintf(stderr, "Partner launcher: %s\n", strerror(errno));
        return 127;
    }

    // App.app/Contents/MacOS/launcher -> directory containing all three apps.
    for (int parent = 0; parent < 4; ++parent) {
        char *separator = strrchr(applications, '/');
        if (!separator) {
            fprintf(stderr, "Partner launcher has an invalid bundle path\n");
            return 127;
        }
        *separator = '\0';
    }

    const char *target_name = IDP_LAUNCH_PROFILE == 0 ? "idp-mcp" : "idp-emu";
    char target[PATH_MAX];
    const int written = snprintf(
        target, sizeof(target),
        "%s/Iskra Delta Partner.app/Contents/MacOS/%s",
        applications, target_name);
    if (written < 0 || (size_t)written >= sizeof(target)) {
        fprintf(stderr, "Partner target path is too long\n");
        return 127;
    }

    const size_t default_count = IDP_LAUNCH_PROFILE == 0 ? 0U
                               : IDP_LAUNCH_PROFILE == 1 ? 5U
                                                        : 3U;
    char **target_argv = calloc((size_t)argc + default_count + 1U,
                                sizeof(*target_argv));
    if (!target_argv) {
        fprintf(stderr, "Partner launcher is out of memory\n");
        return 127;
    }

    size_t next = 0;
    target_argv[next++] = target;
    if (IDP_LAUNCH_PROFILE == 1) {
        target_argv[next++] = "--model";
        target_argv[next++] = "crt";
        target_argv[next++] = "--system-floppy";
        target_argv[next++] = "--boot";
        target_argv[next++] = "floppy";
    } else if (IDP_LAUNCH_PROFILE == 2) {
        target_argv[next++] = "--model";
        target_argv[next++] = "gdp";
        target_argv[next++] = "--system-hdd";
    }
    for (int argument = 1; argument < argc; ++argument) {
        if (strncmp(argv[argument], "-psn_", 5) != 0)
            target_argv[next++] = argv[argument];
    }

    execv(target, target_argv);
    fprintf(stderr, "Partner launcher could not start %s: %s\n",
            target, strerror(errno));
    free(target_argv);
    return 127;
}
