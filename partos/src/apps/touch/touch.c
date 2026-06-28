#include "../lib/partos.h"

static char touch_path[APP_PATH_CAP];
static pa_file_t touch_file;

static const char touch_usage_text[] = "usage: touch PATH\r\n";
static const char touch_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(touch_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, touch_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(touch_path, APP_PATH_CAP, touch_path)) {
        goto touch_usage;
    }

    if (app_create_file(fs, touch_path, &touch_file) != FAT_OK) {
        goto touch_error;
    }
    if (app_wait_status(&touch_file.status) != FAT_OK) {
        goto touch_error;
    }
    return 0;

touch_usage:
    app_write_cstr(touch_usage_text);
    return 1;

touch_error:
    app_write_cstr(touch_error_text);
    return 1;
}
