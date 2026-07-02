#include "../lib/libc.h"

extern int16_t shell_libc_write_buffer(const void *buf, uint16_t len);
extern int16_t shell_libc_write_newline(void);

uint16_t shell_libc_strlen(const char *s)
{
    uint16_t len = 0u;

    if (s == 0) {
        return 0u;
    }
    while (s[len] != 0) {
        len++;
    }
    return len;
}

int16_t shell_libc_strcmp(const char *lhs, const char *rhs)
{
    if (lhs == 0) {
        lhs = "";
    }
    if (rhs == 0) {
        rhs = "";
    }
    while ((*lhs != 0) && (*lhs == *rhs)) {
        lhs++;
        rhs++;
    }
    if (*lhs < *rhs) {
        return -1;
    }
    if (*lhs > *rhs) {
        return 1;
    }
    return 0;
}

int16_t shell_libc_strncmp(const char *lhs, const char *rhs, uint16_t n)
{
    if (n == 0u) {
        return 0;
    }
    if (lhs == 0) {
        lhs = "";
    }
    if (rhs == 0) {
        rhs = "";
    }
    while ((n > 1u) && (*lhs != 0) && (*lhs == *rhs)) {
        lhs++;
        rhs++;
        n--;
    }
    if (*lhs < *rhs) {
        return -1;
    }
    if (*lhs > *rhs) {
        return 1;
    }
    return 0;
}

char *shell_libc_strcpy(char *dst, const char *src)
{
    char *out = dst;

    if ((dst == 0) || (src == 0)) {
        return dst;
    }
    while (*src != 0) {
        *dst++ = *src++;
    }
    *dst = 0;
    return out;
}

void *shell_libc_memcpy(void *dst, const void *src, uint16_t n)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    uint16_t i;

    if ((dst == 0) || (src == 0)) {
        return dst;
    }
    for (i = 0u; i < n; ++i) {
        out[i] = in[i];
    }
    return dst;
}

void *shell_libc_memset(void *dst, int16_t value, uint16_t n)
{
    uint8_t *out = (uint8_t *)dst;
    uint16_t i;

    if (dst == 0) {
        return 0;
    }
    for (i = 0u; i < n; ++i) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

int16_t shell_libc_write(const void *buf, uint16_t len)
{
    return shell_libc_write_buffer(buf, len);
}

int16_t shell_libc_putchar(int16_t ch)
{
    char c = (char)ch;

    (void)shell_libc_write_buffer(&c, 1u);
    return (int16_t)((uint8_t)c);
}

int16_t shell_libc_puts(const char *s)
{
    if (s != 0) {
        (void)shell_libc_write_buffer(s, shell_libc_strlen(s));
    }
    (void)shell_libc_write_newline();
    return 0;
}
