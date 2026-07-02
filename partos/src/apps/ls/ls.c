#include "../lib/libc.h"

static char ls_path[APP_PATH_CAP];
static fat_dirinfo_t ls_dirinfo;
static fat_fs_t *ls_fs;
static char ls_name[14];
static char ls_size[11];
static char ls_char_buf[1];

static const char ls_usage_text[] = "usage: ls [/PATH]";
static const char ls_error_text[] = "?";
static const char ls_dir_size_text[] = "<dir>";

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

static uint8_t ls_build_name(const fat_dirinfo_t *info, char *dst, uint8_t cap)
{
    uint8_t src = 0u;
    uint8_t out = 0u;
    uint8_t has_ext = 0u;

    if ((info == 0) || (dst == 0) || (cap < 2u)) {
        return 0u;
    }

    while ((src < 8u) && (info->name[src] != ' ')) {
        if ((uint8_t)(out + 1u) >= cap) {
            return 0u;
        }
        dst[out++] = ls_ascii_lower(info->name[src++]);
    }

    while (src < 11u) {
        if (info->name[src] != ' ') {
            has_ext = 1u;
            break;
        }
        src++;
    }

    if (has_ext != 0u) {
        if ((uint8_t)(out + 1u) >= cap) {
            return 0u;
        }
        dst[out++] = '.';
        while (src < 11u) {
            if (info->name[src] != ' ') {
                if ((uint8_t)(out + 1u) >= cap) {
                    return 0u;
                }
                dst[out++] = ls_ascii_lower(info->name[src]);
            }
            src++;
        }
    }

    if ((info->attr & FAT_ATTR_DIRECTORY) != 0u) {
        if ((out == 0u) || (dst[out - 1u] != '/')) {
            if ((uint8_t)(out + 1u) >= cap) {
                return 0u;
            }
            dst[out++] = '/';
        }
    }

    dst[out] = 0;
    return 1u;
}

static void ls_format_size(uint32_t value, char *dst)
{
    static const uint32_t powers[10] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL
    };
    uint8_t started = 0u;
    uint8_t i;

    for (i = 0u; i != 10u; ++i) {
        uint8_t digit = 0u;

        while (value >= powers[i]) {
            value -= powers[i];
            digit++;
        }
        if ((digit != 0u) || (started != 0u) || (i == 9u)) {
            *dst++ = (char)('0' + digit);
            started = 1u;
        }
    }

    *dst = 0;
}

static void ls_write_size(uint32_t value)
{
    ls_format_size(value, ls_size);
    (void)write(ls_size, strlen(ls_size));
}

static uint8_t ls_write_entry(const fat_dirinfo_t *info)
{
    if (!ls_build_name(info, ls_name, sizeof(ls_name))) {
        return 0u;
    }

    ls_write_char(((info->attr & FAT_ATTR_DIRECTORY) != 0u) ? 'd' : '-');
    (void)write("  ", 2u);
    if ((info->attr & FAT_ATTR_DIRECTORY) != 0u) {
        (void)write(ls_dir_size_text, strlen(ls_dir_size_text));
    } else {
        ls_write_size(info->size);
    }
    (void)write("  ", 2u);
    (void)write(ls_name, strlen(ls_name));
    (void)puts("");
    return 1u;
}

static uint8_t ls_prepare_path(void)
{
    if (app_argc() == 1) {
        return app_resolve_path(ls_path, APP_PATH_CAP, ".");
    }
    if (app_argc() != 2) {
        return 0u;
    }
    return app_resolve_path(ls_path, APP_PATH_CAP, app_argv()[1]);
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

        if ((ls_dirinfo.attr & FAT_ATTR_VOLUME_ID) != 0u) {
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
