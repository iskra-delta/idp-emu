#include "../lib/partos.h"

static char rmdir_path[APP_PATH_CAP];
static pa_dirent_t rmdir_result;

static const char rmdir_usage_text[] = "usage: rmdir PATH\r\n";
static const char rmdir_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(rmdir_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, rmdir_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(rmdir_path, APP_PATH_CAP, rmdir_path)) {
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
    app_write_cstr(rmdir_usage_text);
    return 1;

rmdir_error:
    app_write_cstr(rmdir_error_text);
    return 1;
}
