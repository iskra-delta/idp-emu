#include "partos.h"

static char app_resolve_input[APP_PATH_CAP];
static char app_resolve_cwd[APP_PATH_CAP];

extern int16_t pa_clear_screen(void);
extern int16_t pa_write_buffer(const void *buf, uint16_t len);
extern void *pa_query_service(const char *name);
extern sys_info_t *pa_get_sys_info(void);
extern fat_fs_t *pa_get_boot_fs(void);
extern char *pa_get_current_dir(void);
extern int16_t pa_set_text_attr(uint8_t attr);
extern int16_t pa_mount_fs(fat_fs_t *fs, const char *dev_name);
extern int16_t pa_lookup_path(fat_fs_t *fs, const char *path, fat_dirent_t *entry);
extern int16_t pa_open_file(fat_fs_t *fs, const char *path, fat_file_t *file);
extern int16_t pa_create_file(fat_fs_t *fs, const char *path, fat_file_t *file);
extern int16_t pa_read_file(fat_file_t *file, void *buf, uint16_t bytes);
extern int16_t pa_write_file(fat_file_t *file, const void *buf, uint16_t bytes);
extern int16_t pa_readdir(fat_fs_t *fs, const char *path, fat_dirinfo_t *info);
extern int16_t pa_unlink_path(fat_fs_t *fs, const char *path, fat_dirent_t *result);
extern int16_t pa_mkdir_path(fat_fs_t *fs, const char *path, fat_dirent_t *result);
extern int16_t pa_rmdir_path(fat_fs_t *fs, const char *path, fat_dirent_t *result);

static char app_hex_nibble(uint8_t value)
{
    value &= 0x0fu;
    if (value < 10u) {
        return (char)('0' + value);
    }
    return (char)('A' + (value - 10u));
}

static uint8_t app_copy_cstr_bounded(char *dst, uint8_t cap, const char *src)
{
    uint8_t len = 0;

    if ((dst == 0) || (src == 0) || (cap == 0u)) {
        return 0u;
    }
    while (*src != 0) {
        if ((uint8_t)(len + 1u) >= cap) {
            dst[0] = 0;
            return 0u;
        }
        dst[len++] = *src++;
    }
    dst[len] = 0;
    return 1u;
}

static uint8_t app_cstr_len(const char *s)
{
    uint8_t len = 0;

    while (s[len] != 0) {
        len++;
    }
    return len;
}

sys_info_t *app_sys_info(void)
{
    return pa_get_sys_info();
}

fat_fs_t *app_boot_filesystem(void)
{
    return pa_get_boot_fs();
}

char *app_current_dir(void)
{
    return pa_get_current_dir();
}

int16_t app_clear_screen(void)
{
    return pa_clear_screen();
}

int16_t app_write_buffer(const void *buf, uint16_t len)
{
    if ((buf == 0) || (len == 0u)) {
        return 0;
    }
    return pa_write_buffer(buf, len);
}

void app_write_cstr(const char *s)
{
    uint16_t len = 0;

    if (s == 0) {
        return;
    }
    while (s[len] != 0) {
        len++;
    }
    if (len != 0u) {
        (void)app_write_buffer(s, len);
    }
}

void app_write_newline(void)
{
    static const char newline[2] = { '\r', '\n' };

    (void)app_write_buffer(newline, 2u);
}

void app_set_text_attr(uint8_t attr)
{
    (void)pa_set_text_attr(attr);
}

void app_write_hex16(uint16_t value)
{
    char buf[7];

    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = app_hex_nibble((uint8_t)(value >> 12));
    buf[3] = app_hex_nibble((uint8_t)(value >> 8));
    buf[4] = app_hex_nibble((uint8_t)(value >> 4));
    buf[5] = app_hex_nibble((uint8_t)value);
    buf[6] = 0;
    app_write_cstr(buf);
}

uint8_t app_read_u8(const void *base, uint8_t offset)
{
    const uint8_t *p = (const uint8_t *)base;

    if (p == 0) {
        return 0u;
    }
    return p[offset];
}

uint16_t app_read_u16(const void *base, uint8_t offset)
{
    const uint8_t *p = (const uint8_t *)base;

    if (p == 0) {
        return 0u;
    }
    return (uint16_t)p[offset] | ((uint16_t)p[offset + 1u] << 8);
}

void app_write_u16(void *base, uint8_t offset, uint16_t value)
{
    uint8_t *p = (uint8_t *)base;

    if (p == 0) {
        return;
    }
    p[offset] = (uint8_t)value;
    p[offset + 1u] = (uint8_t)(value >> 8);
}

int16_t app_mount_fs(fat_fs_t *fs, const char *dev_name)
{
    if ((fs == 0) || (dev_name == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_mount_fs(fs, dev_name);
}

int16_t app_lookup_path(fat_fs_t *fs, const char *path, fat_dirent_t *entry)
{
    if ((fs == 0) || (path == 0) || (entry == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_lookup_path(fs, path, entry);
}

int16_t app_open_file(fat_fs_t *fs, const char *path, fat_file_t *file)
{
    if ((fs == 0) || (path == 0) || (file == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_open_file(fs, path, file);
}

int16_t app_create_file(fat_fs_t *fs, const char *path, fat_file_t *file)
{
    if ((fs == 0) || (path == 0) || (file == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_create_file(fs, path, file);
}

int16_t app_read_file(fat_file_t *file, void *buf, uint16_t bytes)
{
    if ((file == 0) || (buf == 0) || (bytes == 0u) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_read_file(file, buf, bytes);
}

int16_t app_write_file(fat_file_t *file, const void *buf, uint16_t bytes)
{
    if ((file == 0) || (buf == 0) || (bytes == 0u) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_write_file(file, buf, bytes);
}

int16_t app_read_directory(fat_fs_t *fs, const char *path, fat_dirinfo_t *info)
{
    if ((fs == 0) || (path == 0) || (info == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_readdir(fs, path, info);
}

int16_t app_unlink_path(fat_fs_t *fs, const char *path, fat_dirent_t *result)
{
    if ((fs == 0) || (path == 0) || (result == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_unlink_path(fs, path, result);
}

int16_t app_mkdir_path(fat_fs_t *fs, const char *path, fat_dirent_t *result)
{
    if ((fs == 0) || (path == 0) || (result == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_mkdir_path(fs, path, result);
}

int16_t app_rmdir_path(fat_fs_t *fs, const char *path, fat_dirent_t *result)
{
    if ((fs == 0) || (path == 0) || (result == 0) || (app_open_event() == 0)) {
        return FAT_EINVAL;
    }
    return pa_rmdir_path(fs, path, result);
}

char *app_skip_spaces(char *s)
{
    while (*s == ' ') {
        s++;
    }
    return s;
}

char *app_arg_start(void)
{
    char *s = (char *)app_command_line();

    s = app_skip_spaces(s);
    while ((*s != 0) && (*s != ' ')) {
        s++;
    }
    return app_skip_spaces(s);
}

uint8_t app_copy_token(char **cursor, char *dst, uint8_t cap)
{
    char *src = app_skip_spaces(*cursor);

    *cursor = src;
    if (cap == 0u) {
        return 0u;
    }
    if (*src == 0) {
        dst[0] = 0;
        return 0u;
    }

    while ((*src != 0) && (*src != ' ')) {
        if (cap <= 1u) {
            dst[0] = 0;
            *cursor = src;
            return 0u;
        }
        *dst++ = *src++;
        cap--;
    }

    *dst = 0;
    *cursor = src;
    return 1u;
}

uint8_t app_require_eol(const char *cursor)
{
    return (uint8_t)(*app_skip_spaces((char *)cursor) == 0);
}

int16_t app_wait_status(volatile int16_t *status)
{
    event_t *evt = app_event();

    if (evt != 0) {
        pa_wait_one();
    }
    if (status == 0) {
        return FAT_EINVAL;
    }
    return *status;
}

uint8_t app_resolve_path(char *dst, uint8_t cap, const char *path)
{
    char *cursor;
    const char *cwd;
    uint8_t len;

    if ((dst == 0) || (path == 0) || (cap < 2u)) {
        return 0u;
    }
    if (!app_copy_cstr_bounded(app_resolve_input, APP_PATH_CAP, path)) {
        return 0u;
    }

    if (app_resolve_input[0] == '/') {
        dst[0] = '/';
        dst[1] = 0;
        len = 1u;
        cursor = app_resolve_input;
    } else {
        cwd = app_current_dir();
        if (cwd == 0) {
            cwd = "/";
        }
        if ((cwd == 0) || (cwd[0] != '/')) {
            cwd = "/";
        }
        if (!app_copy_cstr_bounded(app_resolve_cwd, APP_PATH_CAP, cwd) ||
            !app_copy_cstr_bounded(dst, cap, app_resolve_cwd)) {
            return 0u;
        }
        len = app_cstr_len(dst);
        if (len == 0u) {
            dst[0] = '/';
            dst[1] = 0;
            len = 1u;
        }
        cursor = app_resolve_input;
    }

    while (*cursor == '/') {
        cursor++;
    }
    while (*cursor != 0) {
        char *segment = cursor;
        uint8_t seg_len = 0;
        uint8_t i;

        while ((*cursor != 0) && (*cursor != '/')) {
            cursor++;
            seg_len++;
        }
        while (*cursor == '/') {
            cursor++;
        }

        if ((seg_len == 1u) && (segment[0] == '.')) {
            continue;
        }
        if ((seg_len == 2u) && (segment[0] == '.') && (segment[1] == '.')) {
            while ((len > 1u) && (dst[len - 1u] != '/')) {
                len--;
            }
            if (len > 1u) {
                len--;
            }
            dst[len] = 0;
            continue;
        }

        if (len > 1u) {
            if ((uint8_t)(len + 1u) >= cap) {
                return 0u;
            }
            dst[len++] = '/';
        }
        if ((uint16_t)len + (uint16_t)seg_len >= (uint16_t)cap) {
            return 0u;
        }
        for (i = 0; i < seg_len; i++) {
            dst[len++] = segment[i];
        }
        dst[len] = 0;
    }

    if (len == 0u) {
        dst[0] = '/';
        dst[1] = 0;
    }
    return 1u;
}
