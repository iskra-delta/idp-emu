#include "../lib/partos.h"

static char rm_path[APP_PATH_CAP];
static pa_dirent_t rm_result;

static const char rm_usage_text[] = "usage: rm PATH\r\n";
static const char rm_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(rm_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, rm_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(rm_path, APP_PATH_CAP, rm_path)) {
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
    app_write_cstr(rm_usage_text);
    return 1;

rm_error:
    app_write_cstr(rm_error_text);
    return 1;
}
