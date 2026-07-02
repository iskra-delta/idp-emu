#include "../lib/libc.h"

static char rm_path[APP_PATH_CAP];
static fat_dirent_t rm_result;

static const char rm_usage_text[] = "usage: rm PATH";
static const char rm_error_text[] = "?";

int main(int argc, char **argv)
{
    fat_fs_t *fs;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        puts(rm_error_text);
        return 1;
    }
    if ((argc != 2) || !app_resolve_path(rm_path, APP_PATH_CAP, argv[1])) {
        goto rm_usage;
    }

    if (app_unlink_path(fs, rm_path, &rm_result) != FAT_OK) {
        goto rm_error;
    }
    if (app_wait_status(&rm_result.status) != FAT_OK) {
        goto rm_error;
    }
    return 0;

rm_usage:
    puts(rm_usage_text);
    return 1;

rm_error:
    puts(rm_error_text);
    return 1;
}
