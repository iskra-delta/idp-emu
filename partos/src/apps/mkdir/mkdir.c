#include "../lib/partos.h"

static char mkdir_path[APP_PATH_CAP];
static pa_dirent_t mkdir_result;

static const char mkdir_usage_text[] = "usage: mkdir PATH\r\n";
static const char mkdir_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(mkdir_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, mkdir_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(mkdir_path, APP_PATH_CAP, mkdir_path)) {
        goto mkdir_usage;
    }

    if (app_mkdir_path(fs, mkdir_path, &mkdir_result) != FAT_OK) {
        goto mkdir_error;
    }
    if (app_wait_status(&mkdir_result.status) != FAT_OK) {
        goto mkdir_error;
    }
    return 0;

mkdir_usage:
    app_write_cstr(mkdir_usage_text);
    return 1;

mkdir_error:
    app_write_cstr(mkdir_error_text);
    return 1;
}
