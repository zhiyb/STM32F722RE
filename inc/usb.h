#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "macros.h"

#ifdef BOOTLOADER
#define USB_INTERFACE_HID       0
#define USB_INTERFACE_CDC       0
#define USB_INTERFACE_CMSIS_DAP 0
#define USB_INTERFACE_DFU_RT    0
#define USB_INTERFACE_DFU_MODE  1
#else
#define USB_INTERFACE_HID       0
#define USB_INTERFACE_CDC       0
#define USB_INTERFACE_CMSIS_DAP 1
#define USB_INTERFACE_DFU_RT    1
#define USB_INTERFACE_DFU_MODE  0
#endif

typedef enum {
    UsbIfFs = 0,
    UsbIfHs,
    UsbNumIfs,
} usb_if_t;

typedef enum {
#if USB_INTERFACE_HID
    UsbInterfaceHid,
#endif
#if USB_INTERFACE_CDC
    UsbInterfaceCDCComm,
    UsbInterfaceCDCData,
#endif
#if USB_INTERFACE_CMSIS_DAP
    UsbInterfaceCmsisDap,
#endif
#if USB_INTERFACE_DFU_RT
    UsbInterfaceDfuRT,
#endif
#if USB_INTERFACE_DFU_MODE
    UsbInterfaceDfuMode = 0,
#endif
    UsbNumInterfaces
} usb_interface_id_t;

typedef enum {
    UsbEpIn0Ctrl = 0,
#if USB_INTERFACE_CMSIS_DAP
    UsbEpInCmsisDap,
#endif
    // UsbEpHid = 4,
    // UsbEpCDCComm = 4,
    // UsbEpCDCData = 5,
    UsbNumInEndpoints
} usb_endpoint_in_t;

typedef enum {
    UsbEpOut0Ctrl = 0,
#if USB_INTERFACE_CMSIS_DAP
    UsbEpOutCmsisDap,
#endif
    UsbNumOutEndpoints
} usb_endpoint_out_t;

typedef enum {
    UsbEpDisabled = 0b00,
    UsbEpStall    = 0b01,
    UsbEpNak      = 0b10,
    UsbEpValid    = 0b11,
} usb_endpoint_status_t;

typedef enum {
    USB_SETUP_REQ_GET_STATUS        = 0,
    USB_SETUP_REQ_CLEAR_FEATURE     = 1,
    USB_SETUP_REQ_SET_FEATURE       = 3,
    USB_SETUP_REQ_SET_ADDRESS       = 5,
    USB_SETUP_REQ_GET_DESCRIPTOR    = 6,
    USB_SETUP_REQ_SET_DESCRIPTOR    = 7,
    USB_SETUP_REQ_GET_CONFIGURATION = 8,
    USB_SETUP_REQ_SET_CONFIGURATION = 9,
    USB_SETUP_REQ_GET_INTERFACE     = 10,
    USB_SETUP_REQ_SET_INTERFACE     = 11,
    USB_SETUP_REQ_SYNCH_FRAME       = 12,
} setup_bRequest_t;

typedef struct PACKED {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint8_t data[0];
} setup_t;

#define SETUP_STALL ((void *)-1)

void usb_init(usb_if_t usb_if);
void usb_connect(usb_if_t usb_if, bool enable);
void usb_process(usb_if_t usb_if);

// const void *usb_hid_setup(setup_t *setup);
// void usb_hid_process(uint32_t now_ms);
// void usb_hid_mouse_move(int8_t x, int8_t y);

// void usb_cdc_init();
// const void *usb_cdc_setup(setup_t *setup);
// bool usb_cdc_data_out(uint32_t *data, uint16_t len);
// uint16_t usb_cdc_rx_available();
// uint8_t usb_cdc_rx_read();
// void usb_cdc_data_in();
// uint16_t usb_cdc_tx_free();
// void usb_cdc_tx_write(uint8_t v);
