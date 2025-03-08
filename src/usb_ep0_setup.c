#include "usb.h"
#include "semihosting.h"

typedef struct __attribute__((packed)) {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint8_t data[0];
} setup_t;

static inline void usb_ep0_setup_standard_device(setup_t *setup, uint32_t len)
{
    // Table 9-3 Standard Device Requests
    switch (setup->bRequest) {
    case 6:     // GET_DESCRIPTOR
        if (setup->bmRequestType == 0x80) {
            uint8_t type = setup->wValue >> 8;
            uint8_t index = setup->wValue;
            uint16_t len = 0;
            const uint8_t *desc = usb_desc_get(type, index, &len);
            len = len < setup->wLength ? len : setup->wLength;
            usb_hw_ep_tx(0, desc, len, true);
        } else {
            DBG_BKPT("Invalid request type");
        }
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
