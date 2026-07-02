#include "../lib/libc.h"

static char del_path[APP_PATH_CAP];
static fat_dirent_t del_result;

static const char del_usage_text[] = "usage: del PATH";
static const char del_error_text[] = "?";

int main(int argc, char **argv)
{
    fat_fs_t *fs;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        puts(del_error_text);
        return 1;
    }
    if ((argc != 2) || !app_resolve_path(del_path, APP_PATH_CAP, argv[1])) {
        goto del_usage;
    }

    if (app_unlink_path(fs, del_path, &del_result) != FAT_OK) {
        goto del_error;
    }
    if (app_wait_status(&del_result.status) != FAT_OK) {
        goto del_error;
    }
    return 0;

del_usage:
    puts(del_usage_text);
    return 1;

del_error:
    puts(del_error_text);
    return 1;
}
