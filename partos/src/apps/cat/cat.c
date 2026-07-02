#include "../lib/libc.h"

static char cat_path[APP_PATH_CAP];
static fat_file_t cat_file;
static uint8_t cat_buf[256];

static const char cat_usage_text[] = "usage: cat PATH";
static const char cat_align_text[] = "only 256-byte aligned files";
static const char cat_error_text[] = "?";

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    uint16_t secs;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        puts(cat_error_text);
        return 1;
    }
    if ((argc != 2) || !app_resolve_path(cat_path, APP_PATH_CAP, argv[1])) {
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
    puts(cat_usage_text);
    return 1;

cat_align:
    puts(cat_align_text);
    return 1;

cat_error:
    puts(cat_error_text);
    return 1;
}
