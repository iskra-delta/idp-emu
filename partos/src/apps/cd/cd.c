#include "../lib/libc.h"

static char cd_path[APP_PATH_CAP];
static char cd_arg[APP_PATH_CAP];
static char cd_dev[6];
static fat_fs_t cd_fs;
static fat_fs_t *cd_lookup_fs;
static fat_dirent_t cd_result;
static const char *cd_token;
static const char *cd_path_arg;
static uint8_t cd_switch_fs;
static uint8_t cd_dev_required;

static const char cd_usage_text[] = "usage: cd [PATH|DEV/PATH]";
static const char cd_error_text[] = "?";

static void cd_restore_current_fs(const fat_fs_t *src)
{
    fat_fs_t *dst = app_boot_filesystem();

    if ((dst != 0) && (src != 0)) {
        (void)memcpy(dst, src, sizeof(*dst));
    }
}

static void cd_exit_now(const char *text)
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
    uint8_t mounted_switch;
    char *cursor;

    (void)argc;
    (void)argv;

    fs = app_boot_filesystem();
    cd_lookup_fs = fs;
    cd_switch_fs = 0u;
    cd_dev_required = 0u;
    mounted_switch = 0u;
    if ((fs == 0) || (app_open_event() == 0)) {
        cd_exit_now(cd_error_text);
        return 1;
    }

    /* Read the single argument straight from the command line (avoids the
     * argc parameter entirely). No argument => change to the root directory. */
    cursor = app_arg_start();
    cursor = app_skip_spaces(cursor);
    if (*cursor == 0) {
        cd_path[0] = '/';
        cd_path[1] = 0;
    } else {
        if (!app_copy_token(&cursor, cd_arg, APP_PATH_CAP) ||
            !app_require_eol(cursor)) {
            goto cd_usage;
        }
        cd_token = cd_arg;
        cd_path_arg = cd_token;

        if (strncmp(cd_token, "/dev/", 5u) == 0) {
            cd_path_arg = cd_token + 5u;
            cd_dev_required = 1u;
        }

        if ((((cd_path_arg[0] == 'h') || (cd_path_arg[0] == 'H')) &&
             ((cd_path_arg[1] == 'd') || (cd_path_arg[1] == 'D')) &&
             (cd_path_arg[2] >= '0') && (cd_path_arg[2] <= '1') &&
             ((cd_path_arg[3] == 0) || (cd_path_arg[3] == '/')))) {
            cd_dev[0] = 's';
            cd_dev[1] = 'd';
            cd_dev[2] = (char)('a' + (cd_path_arg[2] - '0'));
            cd_dev[3] = 0;
            cd_path_arg += 3u;
            cd_switch_fs = 1u;
        } else if ((((cd_path_arg[0] == 's') || (cd_path_arg[0] == 'S')) &&
                    ((cd_path_arg[1] == 'd') || (cd_path_arg[1] == 'D')) &&
                    (((cd_path_arg[2] >= 'a') && (cd_path_arg[2] <= 'b')) ||
                     ((cd_path_arg[2] >= 'A') && (cd_path_arg[2] <= 'B'))) &&
                    ((cd_path_arg[3] == 0) || (cd_path_arg[3] == '/')))) {
            cd_dev[0] = 's';
            cd_dev[1] = 'd';
            if ((cd_path_arg[2] >= 'A') && (cd_path_arg[2] <= 'B')) {
                cd_dev[2] = (char)(cd_path_arg[2] - 'A' + 'a');
            } else {
                cd_dev[2] = cd_path_arg[2];
            }
            cd_dev[3] = 0;
            cd_path_arg += 3u;
            cd_switch_fs = 1u;
        } else if ((((cd_path_arg[0] == 'f') || (cd_path_arg[0] == 'F')) &&
                    ((cd_path_arg[1] == 'd') || (cd_path_arg[1] == 'D')) &&
                    (cd_path_arg[2] >= '0') && (cd_path_arg[2] <= '3') &&
                    ((cd_path_arg[3] == 0) || (cd_path_arg[3] == '/')))) {
            cd_dev[0] = 'f';
            cd_dev[1] = 'd';
            cd_dev[2] = cd_path_arg[2];
            cd_dev[3] = 0;
            cd_path_arg += 3u;
            cd_switch_fs = 1u;
        } else if (cd_dev_required != 0u) {
            goto cd_usage;
        }

        if (cd_switch_fs != 0u) {
            (void)memcpy(&cd_fs, fs, sizeof(cd_fs));
            if (*cd_path_arg == 0) {
                cd_path[0] = '/';
                cd_path[1] = 0;
            } else if (!app_resolve_path(cd_path, APP_PATH_CAP, cd_path_arg)) {
                goto cd_usage;
            }
            if (app_mount_fs(fs, cd_dev) != FAT_OK) {
                goto cd_error;
            }
            mounted_switch = 1u;
            if (app_wait_status(&fs->status) != FAT_OK) {
                goto cd_error;
            }
            cd_lookup_fs = fs;
        } else if (!app_resolve_path(cd_path, APP_PATH_CAP, cd_token)) {
            goto cd_usage;
        }
    }

    if (app_lookup_path(cd_lookup_fs, cd_path, &cd_result) != FAT_OK) {
        goto cd_error;
    }
    if (app_wait_status(&cd_result.status) != FAT_OK) {
        goto cd_error;
    }
    if ((cd_result.attr & FAT_ATTR_DIRECTORY) == 0u) {
        goto cd_error;
    }
    app_store_current_dir(cd_path);
    cd_exit_now(0);
    return 0;

cd_usage:
    if (mounted_switch != 0u) {
        cd_restore_current_fs(&cd_fs);
    }
    cd_exit_now(cd_usage_text);
    return 1;

cd_error:
    if (mounted_switch != 0u) {
        cd_restore_current_fs(&cd_fs);
    }
    cd_exit_now(cd_error_text);
    return 1;
}
