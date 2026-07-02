#include "partos.h"

static char app_resolve_input[APP_PATH_CAP];
static char app_resolve_cwd[APP_PATH_CAP];

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

/* ---- service-table wrappers (call the live partos_t through app_partos) ---- */

sys_info_t *app_sys_info(void)
{
    partos_t *partos = app_partos();

    if ((partos == 0) || (partos->get_sys_info == 0)) {
        return 0;
    }
    return partos->get_sys_info();
}

fat_fs_t *app_boot_filesystem(void)
{
    partos_t *partos = app_partos();

    if ((partos == 0) || (partos->get_boot_filesystem == 0)) {
        return 0;
    }
    return partos->get_boot_filesystem();
}

char *app_current_dir(void)
{
    partos_t *partos = app_partos();

    if ((partos == 0) || (partos->get_current_dir == 0)) {
        return 0;
    }
    return partos->get_current_dir();
}

int16_t app_clear_screen(void)
{
    partos_t *partos = app_partos();

    if ((partos == 0) || (partos->clear_screen == 0)) {
        return 0;
    }
    return partos->clear_screen();
}

int16_t app_write_buffer(const void *buf, uint16_t len)
{
    partos_t *partos = app_partos();

    if ((buf == 0) || (len == 0u)) {
        return 0;
    }
    if ((partos == 0) || (partos->write_console == 0)) {
        return 0;
    }
    return partos->write_console(buf, len);
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
    partos_t *partos = app_partos();

    if ((partos != 0) && (partos->set_text_attr != 0)) {
        (void)partos->set_text_attr(attr);
    }
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

/* ---- pure command-line / path helpers -------------------------------------- */

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
    int16_t s;
    int16_t s2;

    if (status == 0) {
        return FAT_EINVAL;
    }

    /* The async FAT worker writes *status exactly once (FAT_EBUSY -> final),
     * from interrupt/worker context. xcc reads this 16-bit volatile as two
     * byte loads, so a single read can straddle that write and yield a torn
     * value (e.g. low byte of EBUSY + high byte of OK = 0x00FD) that is
     * neither FAT_EBUSY nor the real result -- causing a spurious '?' even
     * though the operation succeeded. Guard with a double-read: since the
     * worker writes only once, at most one of two consecutive reads can be
     * torn, so we only trust a value that reads back identically. */
    for (;;) {
        s = *status;
        s2 = *status;
        if (s == s2) {
            if (s != FAT_EBUSY) {
                return s;
            }
            app_wait_one();
        }
    }
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

        /* NOTE: ".." is tested before ".". xcc (<= 1.9.3) miscompiles the
         * (seg_len == 1u) comparison in the "." test into (seg_len != 0u),
         * which would otherwise swallow the ".." segment (seg_len == 2) and
         * break "cd ..". Testing ".." first lets its correct seg_len == 2
         * branch pop the path before the buggy "." test can match. See
         * tools/xcc/XCC_BUGS.md (BUG: seg_len==1 folded to seg_len!=0). */
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
        if ((seg_len == 1u) && (segment[0] == '.')) {
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
