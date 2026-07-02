#include "../lib/libc.h"

static char cp_src_path[APP_PATH_CAP];
static char cp_dst_path[APP_PATH_CAP];
static fat_file_t cp_src_file;
static fat_file_t cp_dst_file;
static uint8_t cp_buf[256];
static uint16_t cp_secs;
static const char cp_usage_text[] = "usage: cp SRC DST";
static const char cp_align_text[] = "only 256-byte aligned files";
static const char cp_error_text[] = "?";

static void cp_exit_now(const char *text)
{
    app_close_event();
    if (text != 0) {
        puts(text);
    }
    app_exit_process();
    app_dead();
}

int main(int argc, char **argv)
{
    fat_fs_t *fs;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        cp_exit_now(cp_error_text);
        return 1;
    }
    if ((argc != 3) ||
        !app_resolve_path(cp_src_path, APP_PATH_CAP, argv[1]) ||
        !app_resolve_path(cp_dst_path, APP_PATH_CAP, argv[2])) {
        goto cp_usage;
    }

    {
        int16_t rc = app_open_file(fs, cp_src_path, &cp_src_file);

        if (rc != FAT_OK) {
            cp_exit_now(cp_error_text);
            return 1;
        }
    }
    {
        int16_t rc = app_wait_status(&cp_src_file.status);

        if (rc != FAT_OK) {
            cp_exit_now(cp_error_text);
            return 1;
        }
    }
    if (!app_file_is_256_aligned(&cp_src_file)) {
        goto cp_align;
    }
    cp_secs = app_file_sector_count(&cp_src_file);

    {
        int16_t rc = app_create_file(fs, cp_dst_path, &cp_dst_file);

        if (rc != FAT_OK) {
            cp_exit_now(cp_error_text);
            return 1;
        }
    }
    {
        int16_t rc = app_wait_status(&cp_dst_file.status);

        if (rc != FAT_OK) {
            cp_exit_now(cp_error_text);
            return 1;
        }
    }

    while (cp_secs != 0u) {
        {
            int16_t rc = app_read_file(&cp_src_file, cp_buf, 256u);

            if (rc != FAT_OK) {
                cp_exit_now(cp_error_text);
                return 1;
            }
        }
        {
            int16_t rc = app_wait_status(&cp_src_file.status);

            if (rc != FAT_OK) {
                cp_exit_now(cp_error_text);
                return 1;
            }
        }
        {
            int16_t rc = app_write_file(&cp_dst_file, cp_buf, 256u);

            if (rc != FAT_OK) {
                cp_exit_now(cp_error_text);
                return 1;
            }
        }
        {
            int16_t rc = app_wait_status(&cp_dst_file.status);

            if (rc != FAT_OK) {
                cp_exit_now(cp_error_text);
                return 1;
            }
        }
        cp_secs--;
    }
    cp_exit_now(0);
    return 0;

cp_usage:
    cp_exit_now(cp_usage_text);
    return 1;

cp_align:
    cp_exit_now(cp_align_text);
    return 1;
}
