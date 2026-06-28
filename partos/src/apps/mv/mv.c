#include "../lib/partos.h"

static char mv_src_path[APP_PATH_CAP];
static char mv_dst_path[APP_PATH_CAP];
static pa_file_t mv_src_file;
static pa_file_t mv_dst_file;
static pa_dirent_t mv_result;
static uint8_t mv_buf[256];

static const char mv_usage_text[] = "usage: mv SRC DST\r\n";
static const char mv_align_text[] = "only 256-byte aligned files\r\n";
static const char mv_error_text[] = "?\r\n";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;
    uint16_t secs;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(mv_error_text);
        return 1;
    }

    cursor = app_arg_start();
    if (!app_copy_token(&cursor, mv_src_path, APP_PATH_CAP) ||
        !app_copy_token(&cursor, mv_dst_path, APP_PATH_CAP) ||
        !app_require_eol(cursor) ||
        !app_resolve_path(mv_src_path, APP_PATH_CAP, mv_src_path) ||
        !app_resolve_path(mv_dst_path, APP_PATH_CAP, mv_dst_path)) {
        goto mv_usage;
    }

    if (app_open_file(fs, mv_src_path, &mv_src_file) != FAT_OK) {
        goto mv_error;
    }
    if (app_wait_status(&mv_src_file.status) != FAT_OK) {
        goto mv_error;
    }
    if (!app_file_is_256_aligned(&mv_src_file)) {
        goto mv_align;
    }
    secs = app_file_sector_count(&mv_src_file);

    if (app_create_file(fs, mv_dst_path, &mv_dst_file) != FAT_OK) {
        goto mv_error;
    }
    if (app_wait_status(&mv_dst_file.status) != FAT_OK) {
        goto mv_error;
    }

    while (secs != 0u) {
        if (app_read_file(&mv_src_file, mv_buf, 256u) != FAT_OK) {
            goto mv_error;
        }
        if (app_wait_status(&mv_src_file.status) != FAT_OK) {
            goto mv_error;
        }
        if (app_write_file(&mv_dst_file, mv_buf, 256u) != FAT_OK) {
            goto mv_error;
        }
        if (app_wait_status(&mv_dst_file.status) != FAT_OK) {
            goto mv_error;
        }
        secs--;
    }

    if (app_unlink_path(fs, mv_src_path, &mv_result) != FAT_OK) {
        goto mv_error;
    }
    if (app_wait_status(&mv_result.status) != FAT_OK) {
        goto mv_error;
    }
    return 0;

mv_usage:
    app_write_cstr(mv_usage_text);
    return 1;

mv_align:
    app_write_cstr(mv_align_text);
    return 1;

mv_error:
    app_write_cstr(mv_error_text);
    return 1;
}
