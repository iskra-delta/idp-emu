#ifndef PARTOS_LIBC_H
#define PARTOS_LIBC_H

#include "app.h"

#ifndef PARTOS_SIZE_T_DEFINED
#define PARTOS_SIZE_T_DEFINED
typedef uint16_t size_t;
#endif

size_t strlen(const char *s);
int strcmp(const char *lhs, const char *rhs);
int strncmp(const char *lhs, const char *rhs, size_t n);
char *strcpy(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int write(const void *buf, size_t len);
int putchar(int ch);
int puts(const char *s);
const char *getenv(const char *name);

#endif
