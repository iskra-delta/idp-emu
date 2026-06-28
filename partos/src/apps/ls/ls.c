#include "../lib/partos.h"

static char ls_path[APP_PATH_CAP];
static char ls_line[18];
static pa_dirinfo_t ls_dirinfo;
static fat_fs_t *ls_fs;

static const char ls_usage_text[] = "usage: ls [/PATH]\r\n";
static const char ls_error_text[] = "?\r\n";

static uint8_t ls_prepare_path(void)
{
    char *cursor = app_arg_start();

    cursor = app_skip_spaces(cursor);
    if (*cursor == 0) {
        return app_resolve_path(ls_path, APP_PATH_CAP, ".");
    }
    if (!app_copy_token(&cursor, ls_path, APP_PATH_CAP) ||
        !app_require_eol(cursor)) {
        return 0u;
    }
    return app_resolve_path(ls_path, APP_PATH_CAP, ls_path);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ls_fs = app_boot_filesystem();
    if ((ls_fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(ls_error_text);
        return 1;
    }
    if (!ls_prepare_path()) {
        goto ls_usage;
    }

    ls_dirinfo.index = 0u;

    for (;;) {
        int16_t status;

        if (app_read_directory(ls_fs, ls_path, &ls_dirinfo) != FAT_OK) {
            goto ls_error;
        }
        status = app_wait_status(&ls_dirinfo.status);
        if (status == FAT_ENOENT) {
            break;
        }
        if (status != FAT_OK) {
            goto ls_error;
        }

        ls_dirinfo.index = (uint16_t)(ls_dirinfo.index + 1u);

        if ((ls_dirinfo.attr & FAT_ATTR_VOLUME_ID) != 0u) {
            continue;
        }
        if (!app_format_dir_line(&ls_dirinfo, ls_line)) {
            goto ls_error;
        }
        app_write_cstr(ls_line);
    }
    return 0;

ls_usage:
    app_write_cstr(ls_usage_text);
    return 1;

ls_error:
    app_write_cstr(ls_error_text);
    return 1;
}
