#pragma once

#include <stdint.h>

typedef enum {
    LogNone,
    LogUSB_Interrupt,
    LogUSB_SOF,
    LogUSB_CHEP,
    LogUSB_CHEP_TX,
    LogUSB_CHEP_RX,
    LogUSB_CHEP_LEN,
} log_type_t;

void log_push(log_type_t type, uint32_t data);
