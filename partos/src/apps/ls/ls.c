#include "../lib/libc.h"

static char ls_path[APP_PATH_CAP];
static char ls_arg[APP_PATH_CAP];
static fat_dirinfo_t ls_dirinfo;
static fat_fs_t *ls_fs;
static char ls_name[14];
static char ls_char_buf[1];

static const char ls_usage_text[] = "usage: ls [/PATH]";
static const char ls_error_text[] = "?";

#define LS_INFO_SIZE_LO_OFF 2u
#define LS_INFO_SIZE_HI_OFF 4u
#define LS_INFO_ATTR_OFF    9u
#define LS_NAME_CAP         14u

static char ls_ascii_lower(char c)
{
    if ((c >= 'A') && (c <= 'Z')) {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static void ls_write_char(char c)
{
    ls_char_buf[0] = c;
    (void)write(ls_char_buf, 1u);
}

static void ls_write_two_spaces(void)
{
    ls_write_char(' ');
    ls_write_char(' ');
}

static void ls_write_newline(void)
{
    ls_write_char('\r');
    ls_write_char('\n');
}

static void ls_write_dir_size(void)
{
    ls_write_char('<');
    ls_write_char('d');
    ls_write_char('i');
    ls_write_char('r');
    ls_write_char('>');
}

static void ls_write_large_size(void)
{
    ls_write_char('>');
    ls_write_char('6');
    ls_write_char('4');
    ls_write_char('k');
}

static void ls_write_name(void)
{
    uint8_t i = 0u;

    while ((i < LS_NAME_CAP) && (ls_name[i] != 0)) {
        ls_write_char(ls_name[i]);
        i++;
    }
}

static uint8_t ls_build_name(const fat_dirinfo_t *info)
{
    uint8_t src = 0u;
    uint8_t out = 0u;
    uint8_t has_ext = 0u;
    uint8_t attr;

    if (info == 0) {
        return 0u;
    }
    attr = app_read_u8(info, LS_INFO_ATTR_OFF);

    while ((src < 8u) && (info->name[src] != ' ')) {
        if ((uint8_t)(out + 1u) >= LS_NAME_CAP) {
            return 0u;
        }
        ls_name[out++] = ls_ascii_lower(info->name[src++]);
    }

    while (src < 11u) {
        if (info->name[src] != ' ') {
            has_ext = 1u;
            break;
        }
        src++;
    }

    if (has_ext != 0u) {
        if ((uint8_t)(out + 1u) >= LS_NAME_CAP) {
            return 0u;
        }
        ls_name[out++] = '.';
        while (src < 11u) {
            if (info->name[src] != ' ') {
                if ((uint8_t)(out + 1u) >= LS_NAME_CAP) {
                    return 0u;
                }
                ls_name[out++] = ls_ascii_lower(info->name[src]);
            }
            src++;
        }
    }

    if ((attr & FAT_ATTR_DIRECTORY) != 0u) {
        if ((out == 0u) || (ls_name[out - 1u] != '/')) {
            if ((uint8_t)(out + 1u) >= LS_NAME_CAP) {
                return 0u;
            }
            ls_name[out++] = '/';
        }
    }

    ls_name[out] = 0;
    return out;
}

static void ls_write_u16(uint16_t value)
{
    static const uint16_t powers[5] = {
        10000u, 1000u, 100u, 10u, 1u
    };
    uint8_t started = 0u;
    uint8_t i;

    for (i = 0u; i != 5u; ++i) {
        uint8_t digit = 0u;

        while (value >= powers[i]) {
            value -= powers[i];
            digit++;
        }
        if ((digit != 0u) || (started != 0u) || (i == 4u)) {
            ls_write_char((char)('0' + digit));
            started = 1u;
        }
    }
}

static uint8_t ls_write_entry(const fat_dirinfo_t *info)
{
    uint8_t attr;
    uint16_t size_lo;
    uint16_t size_hi;

    if (ls_build_name(info) == 0u) {
        return 0u;
    }
    attr = app_read_u8(info, LS_INFO_ATTR_OFF);

    ls_write_char(((attr & FAT_ATTR_DIRECTORY) != 0u) ? 'd' : '-');
    ls_write_two_spaces();
    if ((attr & FAT_ATTR_DIRECTORY) != 0u) {
        ls_write_dir_size();
    } else {
        size_lo = app_read_u16(info, LS_INFO_SIZE_LO_OFF);
        size_hi = app_read_u16(info, LS_INFO_SIZE_HI_OFF);
        if (size_hi != 0u) {
            ls_write_large_size();
        } else {
            ls_write_u16(size_lo);
        }
    }
    ls_write_two_spaces();
    ls_write_name();
    ls_write_newline();
    return 1u;
}

static uint8_t ls_prepare_path(void)
{
    char *cursor = app_arg_start();

    if (*cursor == 0) {
        return app_resolve_path(ls_path, APP_PATH_CAP, ".");
    }
    if (!app_copy_token(&cursor, ls_arg, APP_PATH_CAP) ||
        !app_require_eol(cursor)) {
        return 0u;
    }
    return app_resolve_path(ls_path, APP_PATH_CAP, ls_arg);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ls_fs = app_boot_filesystem();
    if ((ls_fs == 0) || (app_open_event() == 0)) {
        puts(ls_error_text);
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

        if ((app_read_u8(&ls_dirinfo, LS_INFO_ATTR_OFF) & FAT_ATTR_VOLUME_ID) != 0u) {
            continue;
        }
        if (!ls_write_entry(&ls_dirinfo)) {
            goto ls_error;
        }
    }
    return 0;

ls_usage:
    puts(ls_usage_text);
    return 1;

ls_error:
    puts(ls_error_text);
    return 1;
}
