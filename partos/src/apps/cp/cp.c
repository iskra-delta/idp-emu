#include "../lib/partos.h"

static char cp_src_path[APP_PATH_CAP];
static char cp_dst_path[APP_PATH_CAP];
static pa_file_t cp_src_file;
static pa_file_t cp_dst_file;
static uint8_t cp_buf[256];

static const char cp_usage_text[] = "usage: cp SRC DST\r\n";
static const char cp_align_text[] = "only 256-byte aligned files\r\n";
static const char cp_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;
    uint16_t secs;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(cp_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, cp_src_path, APP_PATH_CAP) ||
        !app_copy_token(&cursor, cp_dst_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(cp_src_path, APP_PATH_CAP, cp_src_path) ||
        !app_resolve_path(cp_dst_path, APP_PATH_CAP, cp_dst_path)) {
        goto cp_usage;
    }

    if (app_open_file(fs, cp_src_path, &cp_src_file) != FAT_OK) {
        goto cp_error;
    }
    if (app_wait_status(&cp_src_file.status) != FAT_OK) {
        goto cp_error;
    }
    if (!app_file_is_256_aligned(&cp_src_file)) {
        goto cp_align;
    }
    secs = app_file_sector_count(&cp_src_file);

    if (app_create_file(fs, cp_dst_path, &cp_dst_file) != FAT_OK) {
        goto cp_error;
    }
    if (app_wait_status(&cp_dst_file.status) != FAT_OK) {
        goto cp_error;
    }

    while (secs != 0u) {
        if (app_read_file(&cp_src_file, cp_buf, 256u) != FAT_OK) {
            goto cp_error;
        }
        if (app_wait_status(&cp_src_file.status) != FAT_OK) {
            goto cp_error;
        }
        if (app_write_file(&cp_dst_file, cp_buf, 256u) != FAT_OK) {
            goto cp_error;
        }
        if (app_wait_status(&cp_dst_file.status) != FAT_OK) {
            goto cp_error;
        }
        secs--;
    }
    return 0;

cp_usage:
    app_write_cstr(cp_usage_text);
    return 1;

cp_align:
    app_write_cstr(cp_align_text);
    return 1;

cp_error:
    app_write_cstr(cp_error_text);
    return 1;
}
