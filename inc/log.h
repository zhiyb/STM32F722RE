#pragma once

#include <stdint.h>

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
    // LogUSB_RX,
    // LogUSB_RX_DATA,
    // LogUSB_OUT_INT,
    // LogUSB_OUT_EP_INT,
    LogUSB_IN,
    // LogUSB_IN_INT,
    // LogUSB_IN_EP_INT,
    LogUSB_SetAddress,
    // LogUSB_SetAddress_INT,
    LogUSB_UsbReset,
    LogUSB_Connect,
} log_type_t;

void log_push(log_type_t type, uint32_t data);
