#include "usb.h"
#include "semihosting.h"

#define REQ_GET_STATUS          0
#define REQ_CLEAR_FEATURE       1
#define REQ_SET_FEATURE         3
#define REQ_SET_ADDRESS         5
#define REQ_GET_DESCRIPTOR      6
#define REQ_SET_DESCRIPTOR      7
#define REQ_GET_CONFIGURATION   8
#define REQ_SET_CONFIGURATION   9
#define REQ_GET_INTERFACE       0
#define REQ_SET_INTERFACE       1
#define REQ_SYNCH_FRAME         2

typedef struct __attribute__((packed)) {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint8_t data[0];
} setup_t;

typedef struct __attribute__((packed)) {
    union {
        uint16_t get_status;
    };
} ret_buf_t;

static ret_buf_t ret_buf __attribute__((aligned(4)));

static inline void usb_ep0_setup_standard_device(setup_t *setup, uint32_t len)
{
    // Table 9-3 Standard Device Requests
    switch (setup->bRequest) {
    case REQ_SET_ADDRESS:
        // setup->bmRequestType == 0
        usb_hw_set_address(setup->wValue);
        // STATUS IN
        usb_hw_ep_tx(0, 0, 0, false);
        break;

    case REQ_GET_DESCRIPTOR:
        // setup->bmRequestType == 0x80
        uint8_t type = setup->wValue >> 8;
        uint8_t index = setup->wValue;
        uint16_t len = 0;
        const uint8_t *desc = usb_desc_get(type, index, &len);
        if (!desc) {
            // Descriptor not applicable, STALL
            usb_hw_ep_tx_stall(0);
        } else {
            // DATA IN
            len = len < setup->wLength ? len : setup->wLength;
            usb_hw_ep_tx(0, desc, len, true);
        }
        break;

    case REQ_SET_CONFIGURATION:
        // No multi-configuration support needed yet
        // STATUS IN
        usb_hw_ep_tx(0, 0, 0, false);
        break;

    case REQ_GET_STATUS:
        ret_buf.get_status = 0x0001;    // Self-powered
        usb_hw_ep_tx(0, &ret_buf, setup->wLength, true);
        break;

    default:
        DBG_BKPT("Unknown request");
        break;
    }
}

void usb_ep0_setup(void *data, uint32_t len)
{
    if (len < 8) {
        DBG_BKPT("Invalid setup data");
        return;
    }

    setup_t *setup = data;
    switch (setup->bmRequestType & 0x7f) {
    case 0x00:
        // Standard device request
        usb_ep0_setup_standard_device(setup, len);
        break;

    default:
        DBG_BKPT("Unknown request type");
    }
}
