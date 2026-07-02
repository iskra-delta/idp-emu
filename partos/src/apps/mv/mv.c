#include "../lib/libc.h"

static char mv_src_path[APP_PATH_CAP];
static char mv_dst_path[APP_PATH_CAP];
static fat_file_t mv_src_file;
static fat_file_t mv_dst_file;
static fat_dirent_t mv_result;
static uint8_t mv_buf[256];

static const char mv_usage_text[] = "usage: mv SRC DST";
static const char mv_align_text[] = "only 256-byte aligned files";
static const char mv_error_text[] = "?";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    uint16_t secs;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        puts(mv_error_text);
        return 1;
    }
    if ((argc != 3) ||
        !app_resolve_path(mv_src_path, APP_PATH_CAP, argv[1]) ||
        !app_resolve_path(mv_dst_path, APP_PATH_CAP, argv[2])) {
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
    puts(mv_usage_text);
    return 1;

mv_align:
    puts(mv_align_text);
    return 1;

mv_error:
    puts(mv_error_text);
    return 1;
}
