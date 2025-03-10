#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PACKED          __attribute__((packed))
#define ALIGNED(v)      __attribute__((aligned(v)))
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))

typedef enum {
    UsbInterfaceHid,
    UsbInterfaceCDCComm,
    UsbInterfaceCDCData,
    UsbNumInterfaces,
} usb_interface_id_t;

typedef enum {
    UsbEp0Ctrl = 0,
    UsbEpHid,
    UsbEpCDCComm,
    UsbEpCDCData,
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

void usb_init();
void usb_connect(bool enable);
bool usb_is_connected();
void usb_process();

void usb_hw_ep_init();
void usb_hw_ep_ctr_irq();
bool usb_hw_act();
void usb_hw_ep_process();
void usb_hw_ep_tx(uint8_t ep, const void *data, uint32_t len, bool status_out);
void usb_hw_ep_tx_stall(uint8_t ep);
void usb_hw_ep_tx_nak(uint8_t ep);
uint32_t *usb_hw_ep_tx_buffer(uint8_t ep, uint16_t *len);
usb_endpoint_status_t usb_hw_ep_tx_status(uint8_t ep);
usb_endpoint_status_t usb_hw_ep_rx_status(uint8_t ep);
void usb_hw_set_address(uint16_t addr);

void usb_ep0_setup(setup_t *setup);

const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len);

const void *usb_hid_setup(setup_t *setup);
void usb_hid_process(uint32_t now_ms);

void usb_cdc_init();
const void *usb_cdc_setup(setup_t *setup);
bool usb_cdc_data_out(uint32_t *data, uint16_t len);
uint16_t usb_cdc_rx_available();
uint8_t usb_cdc_rx_read();
void usb_cdc_data_in();
uint16_t usb_cdc_tx_free();
void usb_cdc_tx_write(uint8_t v);
