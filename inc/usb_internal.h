#pragma once
#include "usb.h"

typedef enum {
    UsbEvNone,
    UsbEvSetup,
    UsbEvIn,
    UsbEvOut,
} usb_ev_id_t;

typedef struct usb_ev_t {
    uint8_t ev; // usb_ev_id_t
    uint8_t ep;
} usb_ev_t;

#define USB_MAX_NUM_EV 16

typedef struct usb_hw_info_t {
    uint32_t base;
    uint16_t ram_size;
    uint8_t num_ep;
    bool use_dma;
} usb_hw_info_t;

extern const usb_hw_info_t usb_hw_ifs[NumUsbIfs];

typedef struct usb_t {
    struct {
        uint16_t fifo_top;
        uint8_t fifo_num;
        // uint8_t daddr;
        // bool daddr_change;
    } hw;
    struct {
        struct {
            void *p;
            uint32_t len;
            uint32_t offset;
            uint32_t pkts;
            uint16_t last_len;
            uint16_t max_size;
        } out;
        struct {
            void *p;
            uint32_t len;
            uint32_t pkts;
            uint16_t max_size;
        } in;
    } ep[UsbNumEndpoints];
    struct {
        // Per the USB 2.0 specification, normally, during a SETUP packet error,
        // a host does not send more than three back-to-back SETUP packets to the same endpoint
        setup_t setup[3];
        uint8_t buf[512];
    } ep0 ALIGNED(4);
    struct {
        uint32_t grxstsp;
    } rx;
    struct {
        volatile usb_ev_t data[USB_MAX_NUM_EV];
        uint8_t wptr, rptr;
    } ev;
} usb_t;

extern usb_t usb_ifs[NumUsbIfs];

void usb_hw_init(usb_if_t usb_if);
void usb_hw_process(usb_if_t usb_if);
void usb_hw_set_address(usb_if_t usb_if, uint16_t addr);

void usb_hw_connect(usb_if_t usb_if, bool enable);
bool usb_hw_is_connected(usb_if_t usb_if);

void usb_hw_ep_init(usb_if_t usb_if);
void usb_hw_ep_out(usb_if_t usb_if, uint32_t epnum, void *data, uint32_t setup, uint32_t pkt, uint32_t len);
bool usb_hw_ep_out_continue(usb_if_t usb_if, uint32_t ep, uint32_t setup, uint32_t pkt);
void usb_hw_ep_in(usb_if_t usb_if, uint8_t ep, const void *data, uint32_t len, bool short_data);
bool usb_hw_ep_in_continue(usb_if_t usb_if, uint8_t ep);

void usb_ep0_init(usb_if_t usb_if);
void usb_ep0_setup(usb_if_t usb_if, bool buf_valid);
void usb_ep0_out(usb_if_t usb_if);

const uint8_t *usb_desc_get(usb_if_t usb_if, uint8_t *desc_buf, uint8_t type, uint8_t index, uint16_t *len);
