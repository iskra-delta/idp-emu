#include "../lib/libc.h"

static char rmdir_path[APP_PATH_CAP];
static fat_dirent_t rmdir_result;

static const char rmdir_usage_text[] = "usage: rmdir PATH";
static const char rmdir_error_text[] = "?";

int main(int argc, char **argv)
{
    fat_fs_t *fs;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        puts(rmdir_error_text);
        return 1;
    }
    if ((argc != 2) || !app_resolve_path(rmdir_path, APP_PATH_CAP, argv[1])) {
        goto rmdir_usage;
    }

    if (app_rmdir_path(fs, rmdir_path, &rmdir_result) != FAT_OK) {
        goto rmdir_error;
    }
    if (app_wait_status(&rmdir_result.status) != FAT_OK) {
        goto rmdir_error;
    }
    return 0;

rmdir_usage:
    puts(rmdir_usage_text);
    return 1;

rmdir_error:
    puts(rmdir_error_text);
    return 1;
}
