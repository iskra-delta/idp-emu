#include "../lib/libc.h"

static char mkdir_path[APP_PATH_CAP];
static fat_dirent_t mkdir_result;

static const char mkdir_usage_text[] = "usage: mkdir PATH";
static const char mkdir_error_text[] = "?";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        puts(mkdir_error_text);
        return 1;
    }
    if ((argc != 2) || !app_resolve_path(mkdir_path, APP_PATH_CAP, argv[1])) {
        goto mkdir_usage;
    }

    if (app_mkdir_path(fs, mkdir_path, &mkdir_result) != FAT_OK) {
        goto mkdir_error;
    }
    if (app_wait_status(&mkdir_result.status) != FAT_OK) {
        goto mkdir_error;
    }
    app_close_event();
    app_exit_process();
    app_dead();
    return 0;

mkdir_usage:
    puts(mkdir_usage_text);
    return 1;

mkdir_error:
    puts(mkdir_error_text);
    return 1;
}
