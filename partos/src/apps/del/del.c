#include "../lib/partos.h"

static char del_path[APP_PATH_CAP];
static pa_dirent_t del_result;

static const char del_usage_text[] = "usage: del PATH\r\n";
static const char del_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(del_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, del_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(del_path, APP_PATH_CAP, del_path)) {
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
    app_write_cstr(del_usage_text);
    return 1;

del_error:
    app_write_cstr(del_error_text);
    return 1;
}
