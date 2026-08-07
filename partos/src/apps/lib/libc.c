#include "libc.h"

static size_t libc_local_strlen(const char *s)
{
    size_t len = 0u;

    if (s == 0) {
        return 0u;
    }
    while (s[len] != 0) {
        len++;
    }
    return len;
}

static int libc_local_strcmp(const char *lhs, const char *rhs)
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

static int libc_local_strncmp(const char *lhs, const char *rhs, size_t n)
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

static const char *libc_local_getenv(const char *name)
{
    const char *env = app_environment();

    if ((name == 0) || (*name == 0) || (env == 0)) {
        return 0;
    }
    while (*env != 0) {
        const char *entry = env;
        const char *needle = name;

        while ((*needle != 0) && (*entry == *needle)) {
            entry++;
            needle++;
        }
        if ((*needle == 0) && (*entry == '=')) {
            return entry + 1;
        }
        while (*env != 0) {
            env++;
        }
        env++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    return libc_local_strlen(s);
}

int strcmp(const char *lhs, const char *rhs)
{
    return libc_local_strcmp(lhs, rhs);
}

int strncmp(const char *lhs, const char *rhs, size_t n)
{
    return libc_local_strncmp(lhs, rhs, n);
}

char *strcpy(char *dst, const char *src)
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

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    size_t i;

    if ((dst == 0) || (src == 0)) {
        return dst;
    }
    for (i = 0u; i < n; ++i) {
        out[i] = in[i];
    }
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *out = (uint8_t *)dst;
    size_t i;

    if (dst == 0) {
        return 0;
    }
    for (i = 0u; i < n; ++i) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

int write(const void *buf, size_t len)
{
    return (int)app_partos()->write_console(buf, len);
}

int putchar(int ch)
{
    char c = (char)ch;

    app_partos()->write_console(&c, 1u);
    return (int)((uint8_t)c);
}

int puts(const char *s)
{
    static const char newline[2] = { '\r', '\n' };

    if (s != 0) {
        (void)write(s, strlen(s));
    }
    (void)write(newline, 2u);
    return 0;
}

const char *getenv(const char *name)
{
    libc_t *libc = app_libc();

    if ((libc != 0) && (libc->getenv != 0)) {
        return libc->getenv(name);
    }
    return libc_local_getenv(name);
}
