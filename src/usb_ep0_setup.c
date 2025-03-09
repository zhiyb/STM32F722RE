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
#define REQ_GET_INTERFACE       10
#define REQ_SET_INTERFACE       11
#define REQ_SYNCH_FRAME         12

typedef struct PACKED {
    union {
        uint16_t get_status;
    };
} ret_buf_t;

static ret_buf_t ret_buf ALIGNED(4);

static inline const void *usb_ep0_setup_standard_req(setup_t *setup)
{
    // Table 9-3 Standard Device Requests
    switch (setup->bRequest) {
    case REQ_SET_ADDRESS:
        // setup->bmRequestType == 0
        usb_hw_set_address(setup->wValue);
        return 0;

    case REQ_GET_DESCRIPTOR: {
        // setup->bmRequestType == 0x80
        uint8_t type = setup->wValue >> 8;
        uint8_t index = setup->wValue;
        uint16_t len = 0;
        const uint8_t *desc = usb_desc_get(type, index, &len);
        if (!desc) {
            // Descriptor not applicable, STALL
            return (void *)-1;
        }
        // DATA IN
        setup->wLength = len < setup->wLength ? len : setup->wLength;
        return desc;
    }

    case REQ_SET_CONFIGURATION:
        // setup->bmRequestType == 0x00
        // No multi-configuration support needed yet
        return 0;

    case REQ_GET_STATUS:
        switch (setup->bmRequestType) {
        case 0x80:
            ret_buf.get_status = 0x0001;    // Self-powered
            return &ret_buf;
        default:
            DBG_BKPT("Unknown request");
            return (void *)-1;
        }

    default:
        DBG_BKPT("Unknown request");
        return (void *)-1;
    }
}

void usb_ep0_setup(setup_t *setup)
{
    void *ret = (void *)-1;
    switch (setup->bmRequestType & 0x7f) {
    case 0x00:
        // Standard device request
        ret = usb_ep0_setup_standard_req(setup);
        break;

    case 0x01:
        // Standard interface request
    case 0x21:
        // Class interface request
        switch (setup->wIndex) {
        case UsbInterfaceHid:
            ret = usb_hid_setup(setup);
            break;
        case UsbInterfaceCDCComm:
            ret = usb_cdc_setup(setup);
            break;
        default:
            DBG_BKPT("Unknown Interface");
        }
        break;

    default:
        DBG_BKPT("Unknown request type");
    }

    if (ret == (void *)-1) {
        // STALL
        usb_hw_ep_tx_stall(0);
    } else if ((setup->bmRequestType & 0x80) != 0 && setup->wLength != 0) {
        // DATA IN
        usb_hw_ep_tx(0, ret, setup->wLength, true);
    } else {
        // STATUS IN
        usb_hw_ep_tx(0, 0, 0, false);
    }
}
