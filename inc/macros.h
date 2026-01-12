#pragma once

#define USED            __attribute__((used))
#define PACKED          __attribute__((packed))
#define ALIGNED(v)      __attribute__((aligned(v)))
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))

#define MIN(a, b)       ((a) >= (b) ? (b) : (a))
#define MAX(a, b)       ((a) >= (b) ? (a) : (b))
