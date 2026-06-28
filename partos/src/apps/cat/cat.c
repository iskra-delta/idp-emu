#include "../lib/partos.h"

static char cat_path[APP_PATH_CAP];
static pa_file_t cat_file;
static uint8_t cat_buf[256];

static const char cat_usage_text[] = "usage: cat PATH\r\n";
static const char cat_align_text[] = "only 256-byte aligned files\r\n";
static const char cat_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;
    uint16_t secs;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(cat_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, cat_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(cat_path, APP_PATH_CAP, cat_path)) {
        goto cat_usage;
    }

    if (app_open_file(fs, cat_path, &cat_file) != FAT_OK) {
        goto cat_error;
    }
    if (app_wait_status(&cat_file.status) != FAT_OK) {
        goto cat_error;
    }
    if (!app_file_is_256_aligned(&cat_file)) {
        goto cat_align;
    }

    secs = app_file_sector_count(&cat_file);
    while (secs != 0u) {
        if (app_read_file(&cat_file, cat_buf, 256u) != FAT_OK) {
            goto cat_error;
        }
        if (app_wait_status(&cat_file.status) != FAT_OK) {
            goto cat_error;
        }
        (void)app_write_buffer(cat_buf, 256u);
        secs--;
    }
    return 0;

cat_usage:
    app_write_cstr(cat_usage_text);
    return 1;

cat_align:
    app_write_cstr(cat_align_text);
    return 1;

cat_error:
    app_write_cstr(cat_error_text);
    return 1;
}
