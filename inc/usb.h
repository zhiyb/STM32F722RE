#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "macros.h"

#define USB_ALT_IF_NONE 0
#define USB_ALT_IF_HID  1
#define USB_ALT_IF_CDC  2
#define USB_ALT_IF      USB_ALT_IF_NONE

typedef enum {
    UsbIfFs = 0,
    UsbIfHs,
    NumUsbIfs,
} usb_if_t;

typedef enum {
#ifndef BOOTLOADER
#if USB_ALT_IF == USB_ALT_IF_HID
    UsbInterfaceHid,
#endif
#if USB_ALT_IF == USB_ALT_IF_CDC
    UsbInterfaceCDCComm,
    UsbInterfaceCDCData,
#endif
#else
    UsbInterfaceDfuRT,
#endif
    UsbNumInterfaces,
    // Special modes
    UsbInterfaceDfuMode = 0,
} usb_interface_id_t;

typedef enum {
    UsbEp0Ctrl = 0,
    UsbEpBtHciEvents = 1,
    UsbEpBtACLData = 2,
    UsbEpBtACLDataIn = 6,   // Double-buffering channel
    UsbEpBtVoice = 3,
    UsbEpBtVoiceIn = 7,     // Double-buffering channel
    // Alternative interfaces
    UsbEpHid = 4,
    UsbEpCDCComm = 4,
    UsbEpCDCData = 5,
} usb_endpoint_t;

typedef enum {
    UsbEpDisabled = 0b00,
    UsbEpStall    = 0b01,
    UsbEpNak      = 0b10,
    UsbEpValid    = 0b11,
} usb_endpoint_status_t;

typedef struct PACKED {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint8_t data[0];
} setup_t;

void usb_init(usb_if_t usb_if);
void usb_connect(usb_if_t usb_if, bool enable);
void usb_process(usb_if_t usb_if);

void usb_hw_ep_in(usb_if_t usb_if, uint8_t ep, const void *data, uint32_t len, bool status_out);
void usb_hw_ep_in_stall(usb_if_t usb_if, uint8_t ep);

// bool usb_is_connected();

// bool usb_hw_ep_tx_db_available(uint8_t ep);
// void usb_hw_ep_tx_db(uint8_t ep, const void *data, uint32_t len);
// void usb_hw_ep_tx_nak(uint8_t ep);
// uint32_t *usb_hw_ep_tx_buffer(uint8_t ep, uint16_t *len);
// usb_endpoint_status_t usb_hw_ep_tx_status(uint8_t ep);
// usb_endpoint_status_t usb_hw_ep_rx_status(uint8_t ep);

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

// void usb_reset_handler();
