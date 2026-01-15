#pragma once
#include "usb.h"

#define USB_DFU_TRANSFER_SIZE   512

typedef enum {
    UsbDfuStatus_OK = 0,
    UsbDfuStatus_errTARGET,
    UsbDfuStatus_errFILE,
    UsbDfuStatus_errWRITE,
    UsbDfuStatus_errERASE,
    UsbDfuStatus_errCHECK_ERASED,
    UsbDfuStatus_errPROG,
    UsbDfuStatus_errVERIFY,
    UsbDfuStatus_errADDRESS,
    UsbDfuStatus_errNOTDONE,
    UsbDfuStatus_errFIRMWARE,
    UsbDfuStatus_errVENDOR,
    UsbDfuStatus_errUSBR,
    UsbDfuStatus_errPOR,
    UsbDfuStatus_errUNKNOWN,
    UsbDfuStatus_errSTALLEDPKT,
} usb_dfu_bStatus_t;

typedef enum {
    UsbDfuState_appIDLE,
    UsbDfuState_appDETACH,
    UsbDfuState_dfuIDLE,
    UsbDfuState_dfuDNLOAD_SYNC,
    UsbDfuState_dfuDNBUSY,
    UsbDfuState_dfuDNLOAD_IDLE,
    UsbDfuState_dfuMANIFEST_SYNC,
    UsbDfuState_dfuMANIFEST,
    UsbDfuState_dfuMANIFEST_WAIT_RESET,
    UsbDfuState_dfuUPLOAD_IDLE,
    UsbDfuState_dfuERROR,
} usb_dfu_bState_t;

usb_dfu_bState_t usb_dfu_state();
void usb_dfu_process();

const void *usb_dfu_setup(setup_t *setup, void *data, uint32_t len);
