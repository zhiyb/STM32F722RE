#pragma once

#define PACKED          __attribute__((packed))
#define ALIGNED(v)      __attribute__((aligned(v)))
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))
