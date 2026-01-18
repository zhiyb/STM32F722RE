#pragma once

#include <stddef.h>
#include <stdint.h>

// zig actually provides this
extern void *memcpy(void *restrict dest, const void *restrict src, size_t n);
extern char *strcpy(char *restrict dst, const char *restrict src);
