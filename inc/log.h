#pragma once

#include <stdint.h>

// #define ENABLE_LOGGING

typedef enum {
    LogNone,

    LogUSB_Interface,
    LogUSB_Interrupt,
    LogUSB_INT_Unhandled,
    LogUSB_INT_UsbReset,
    LogUSB_INT_EunmDone,
    LogUSB_INT_Rx,
    LogUSB_INT_In,
    LogUSB_INT_InEp,
    LogUSB_INT_Out,
    LogUSB_INT_OutEp,
    LogUSB_In,
    LogUSB_InContinue,
    LogUSB_Out,
    LogUSB_OutContinue,
    LogUSB_IN,
    LogUSB_SetAddress,
    LogUSB_UsbReset,
    LogUSB_Connect,

    LogUsbDfu_Download,
    LogUsbDfu_Manifest,

    LogFlash_INT,
} log_type_t;

#ifdef ENABLE_LOGGING
void log_push(log_type_t type, uint32_t data);
#else
static inline void log_push(log_type_t type, uint32_t data) {}
#endif
