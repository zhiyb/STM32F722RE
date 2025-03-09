#include "usb.h"
#include "usb_desc_hid.h"
#include "semihosting.h"

// Standard requests
#define REQ_STD_GET_DESCRIPTOR  6
#define REQ_STD_SET_DESCRIPTOR  7

// Class-specific requests
#define REQ_CLASS_GET_REPORT      0x01
#define REQ_CLASS_GET_IDLE        0x02
#define REQ_CLASS_GET_PROTOCOL    0x03
#define REQ_CLASS_SET_REPORT      0x09
#define REQ_CLASS_SET_IDLE        0x0a
#define REQ_CLASS_SET_PROTOCOL    0x0b

const void *usb_hid_setup(setup_t *setup, uint32_t len)
{
    switch (setup->bRequest) {
    case REQ_STD_GET_DESCRIPTOR: {
        // bmRequestType == 0x81
        uint8_t type = setup->wValue >> 8;
        uint8_t index = setup->wValue;
        if (type != DESC_TYPE_HID_REPORT) {
            DBG_BKPT("Unknown descriptor");
            return (void *)-1;
        }
        const uint16_t len = sizeof(hidReportDescriptor);
        setup->wLength = len < setup->wLength ? len : setup->wLength;
        return hidReportDescriptor;
    }

    case REQ_CLASS_SET_IDLE: {
        // bmRequestType == 0x21
        // The Set_Idle request silences a particular report on the Interrupt In pipe
        // until a new event occurs or the specified amount of time passes.
        uint8_t duration = setup->wValue >> 8;
        uint8_t id = setup->wValue;
        // TODO();
        return 0;
    }
    default:
        DBG_BKPT("Unknown request");
        return (void *)-1;
    }
}
