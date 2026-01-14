#pragma once

#define NVIC_PRIORITY_GROUPING	3	// 4+4 bits

#define NVIC_PRIORITY_PVD	0, 0

typedef enum {
    NvicPriorityFault,
    NvicPriorityUsbHsHP,
    NvicPriorityUsbHsLP,
    NvicPriorityUsbFs,
} nvic_priority_t;

extern const void * const irq_vectors[];
