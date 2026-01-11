#pragma once

#include <stdint.h>

typedef enum {
    LogNone,
    LogUSB_Interface,
    LogUSB_Interrupt,
    LogUSB_RX,
    LogUSB_RX_DATA,
    LogUSB_OUT_INT,
    LogUSB_OUT_EP_INT,
    LogUSB_IN,
    LogUSB_IN_INT,
    LogUSB_IN_EP_INT,
    LogUSB_SetAddress,
    LogUSB_SetAddress_INT,
    LogUSB_SOF,
    LogUSB_CHEP,
    LogUSB_CHEP_TX,
    LogUSB_CHEP_RX,
    LogUSB_CHEP_LEN,
} log_type_t;

void log_push(log_type_t type, uint32_t data);
