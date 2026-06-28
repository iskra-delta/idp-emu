#include "../lib/partos.h"

static char cd_path[APP_PATH_CAP];
static char cd_dev[6];
static pa_fs_t cd_fs;
static pa_dirent_t cd_result;

static const char cd_usage_text[] = "usage: cd [PATH|DEV/PATH]\r\n";
static const char cd_error_text[] = "?\r\n";

static uint8_t cd_match_prefix(const char *text, const char *prefix)
{
    while (*prefix != 0) {
        if (*text++ != *prefix++) {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t cd_extract_dev(const char *path, const char **rest)
{
    uint8_t len = 0;

    while ((path[len] != 0) && (path[len] != '/')) {
        if (len >= 5u) {
            return 0u;
        }
        cd_dev[len] = path[len];
        len++;
    }
    if (len == 0u) {
        return 0u;
    }
    cd_dev[len] = 0;
    *rest = path + len;
    return 1u;
}

static uint8_t cd_normalize_dev(void)
{
    if (((cd_dev[0] == 'h') || (cd_dev[0] == 'H')) &&
        ((cd_dev[1] == 'd') || (cd_dev[1] == 'D')) &&
        (cd_dev[2] >= '0') && (cd_dev[2] <= '1') &&
        (cd_dev[3] == 0)) {
        cd_dev[0] = 's';
        cd_dev[1] = 'd';
        cd_dev[2] = (char)('a' + (cd_dev[2] - '0'));
        return 1u;
    }
    if (((cd_dev[0] == 's') || (cd_dev[0] == 'S')) &&
        ((cd_dev[1] == 'd') || (cd_dev[1] == 'D')) &&
        ((cd_dev[2] >= 'a') && (cd_dev[2] <= 'b') ||
         (cd_dev[2] >= 'A') && (cd_dev[2] <= 'B')) &&
        (cd_dev[3] == 0)) {
        cd_dev[0] = 's';
        cd_dev[1] = 'd';
        if ((cd_dev[2] >= 'A') && (cd_dev[2] <= 'B')) {
            cd_dev[2] = (char)(cd_dev[2] - 'A' + 'a');
        }
        return 1u;
    }
    if (((cd_dev[0] == 'f') || (cd_dev[0] == 'F')) &&
        ((cd_dev[1] == 'd') || (cd_dev[1] == 'D')) &&
        (cd_dev[2] >= '0') && (cd_dev[2] <= '3') &&
        (cd_dev[3] == 0)) {
        cd_dev[0] = 'f';
        cd_dev[1] = 'd';
        return 1u;
    }
    return 0u;
}

static uint8_t cd_parse_target(const char *token, uint8_t *switch_fs)
{
    const char *rest = 0;
    const char *dev_path = token;

    *switch_fs = 0u;
    if (cd_match_prefix(token, "/dev/")) {
        dev_path = token + 5;
        if (!cd_extract_dev(dev_path, &rest) || !cd_normalize_dev()) {
            return 0u;
        }
        *switch_fs = 1u;
    } else if (cd_extract_dev(dev_path, &rest) && cd_normalize_dev()) {
        *switch_fs = 1u;
    }

    if (*switch_fs != 0u) {
        if (*rest == 0) {
            cd_path[0] = '/';
            cd_path[1] = 0;
            return 1u;
        }
        return app_resolve_path(cd_path, APP_PATH_CAP, rest);
    }
    return app_resolve_path(cd_path, APP_PATH_CAP, token);
}

static void cd_store_current_dir(const char *path)
{
    char *dst = app_current_dir();

    if (dst == 0) {
        return;
    }
    while (*path != 0) {
        *dst++ = *path++;
    }
    *dst = 0;
}

static void cd_commit_current_fs(const pa_fs_t *src)
{
    pa_fs_t *dst = app_boot_filesystem();

    if (dst == 0) {
        return;
    }
    dst->dev = src->dev;
    dst->lba_base = src->lba_base;
    dst->total_sectors = src->total_sectors;
    dst->reserved_sectors = src->reserved_sectors;
    dst->sectors_per_fat = src->sectors_per_fat;
    dst->root_entries = src->root_entries;
    dst->root_dir_sectors = src->root_dir_sectors;
    dst->fat_start = src->fat_start;
    dst->root_start = src->root_start;
    dst->data_start = src->data_start;
    dst->total_clusters = src->total_clusters;
    dst->alloc_hint = src->alloc_hint;
    dst->sectors_per_cluster = src->sectors_per_cluster;
    dst->num_fats = src->num_fats;
    dst->fat_bits = src->fat_bits;
    dst->mounted = src->mounted;
    dst->status = src->status;
}

int main(int argc, char **argv)
{
    fat_fs_t *fs;
    char *cursor;
    uint8_t switch_fs;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    if ((fs == 0) || (app_open_event() == 0)) {
        app_write_cstr(cd_error_text);
        return 1;
    }

    cursor = app_arg_start();
    cursor = app_skip_spaces(cursor);
    if (*cursor == 0) {
        cd_path[0] = '/';
        cd_path[1] = 0;
        switch_fs = 0u;
    } else {
        if (!app_copy_token(&cursor, cd_path, APP_PATH_CAP) ||
            !app_require_eol(cursor) ||
            !cd_parse_target(cd_path, &switch_fs)) {
            goto cd_usage;
        }
        if (switch_fs != 0u) {
            if (app_mount_fs(&cd_fs, cd_dev) != FAT_OK) {
                goto cd_error;
            }
            if (app_wait_status(&cd_fs.status) != FAT_OK) {
                goto cd_error;
            }
            cd_commit_current_fs(&cd_fs);
        }
    }

    if (app_lookup_path(fs, cd_path, &cd_result) != FAT_OK) {
        goto cd_error;
    }
    if (app_wait_status(&cd_result.status) != FAT_OK) {
        goto cd_error;
    }
    if ((cd_result.attr & FAT_ATTR_DIRECTORY) == 0u) {
        goto cd_error;
    }
    cd_store_current_dir(cd_path);
    return 0;

cd_usage:
    app_write_cstr(cd_usage_text);
    return 1;

cd_error:
    app_write_cstr(cd_error_text);
    return 1;
}
