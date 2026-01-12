#include "semihosting.h"
#include "macros.h"
#include "usb.h"
#include "usb_internal.h"
#include "usb_dfu.h"

typedef struct PACKED {
    union {
        uint16_t get_status;
    };
} ret_buf_t;

static ret_buf_t ret_buf ALIGNED(4);

void usb_ep0_init(usb_if_t usb_if)
{
    usb_t *usb = &usb_ifs[usb_if];
    // Ready to receive a setup packet
    usb_hw_ep_out(usb_if, 0, &usb->ep0.setup, 1, 0, 0);
}

static inline const void *usb_ep0_setup_standard_req(usb_if_t usb_if, setup_t *setup)
{
    usb_t *usb = &usb_ifs[usb_if];

    // Table 9-3 Standard Device Requests
    switch (setup->bRequest) {
    case USB_SETUP_REQ_CLEAR_FEATURE:
        return 0;

    case USB_SETUP_REQ_SET_FEATURE:
        return 0;

    case USB_SETUP_REQ_SET_ADDRESS:
        // setup->bmRequestType == 0
        usb_hw_set_address(usb_if, setup->wValue);
        return 0;

    case USB_SETUP_REQ_GET_DESCRIPTOR: {
        // setup->bmRequestType == 0x80
        uint8_t type = setup->wValue >> 8;
        uint8_t index = setup->wValue;
        uint16_t len = 0;
        const uint8_t *desc = usb_desc_get(usb->desc_buf, type, index, &len);
        if (!desc) {
            // Descriptor not applicable, STALL
            return SETUP_STALL;
        }
        // DATA IN
        setup->wLength = len < setup->wLength ? len : setup->wLength;
        return desc;
    }

    case USB_SETUP_REQ_SET_CONFIGURATION:
        // setup->bmRequestType == 0x00
        // No multi-configuration support needed yet
        return 0;

    case USB_SETUP_REQ_GET_STATUS:
        switch (setup->bmRequestType) {
        case 0x80:
            ret_buf.get_status = 0x0001;    // Self-powered
            return &ret_buf;
        default:
            DBG_BKPT("Unknown request");
            return SETUP_STALL;
        }

    default:
        DBG_BKPT("Unknown request");
        return SETUP_STALL;
    }
}

void usb_ep0_setup(usb_if_t usb_if)
{
    usb_t *usb = &usb_ifs[usb_if];
    setup_t *setup = &usb->ep0.setup;
    uint16_t wLength = setup->wLength;
    if (!(setup->bmRequestType & 0x80) && wLength) {
        // Ready to receive data stage
        TODO();
    }

    const void *ret = SETUP_STALL;
    switch (setup->bmRequestType & 0x7f) {
    case 0x00:
        // Standard device request
        ret = usb_ep0_setup_standard_req(usb_if, setup);
        break;

    case 0x01:
        // Standard interface request
    case 0x21:
        // Class interface request
#ifdef BOOTLOADER
        if (usb_dfu_state() >= UsbDfuState_dfuIDLE) {
            switch (setup->wIndex) {
            case UsbInterfaceDfuMode:
                ret = usb_dfu_setup(&usb->dfu, setup);
                break;
            default:
                DBG_BKPT("Unknown Interface");
            }
        } else {
            switch (setup->wIndex) {
            case UsbInterfaceDfuRT:
                ret = usb_dfu_setup(&usb->dfu, setup);
                break;
            default:
                DBG_BKPT("Unknown Interface");
            }
        }
#else
        switch (setup->wIndex) {
#if USB_ALT_IF == USB_ALT_IF_HID
        case UsbInterfaceHid:
            ret = usb_hid_setup(setup);
            break;
#endif
#if USB_ALT_IF == USB_ALT_IF_CDC
        case UsbInterfaceCDCComm:
            ret = usb_cdc_setup(setup);
            break;
#endif
        default:
            DBG_BKPT("Unknown Interface");
        }
#endif
        break;

//     case 0x20:
//         // Host-to-device, class, device request
//         // Special request type for bluetooth HCI Commands
//         ret = bt_hci_usb_setup(setup);
//         break;

    default:
        DBG_BKPT("Unknown request type");
    }

    uint32_t status_out = 0;
    if (ret == SETUP_STALL) {
        // STALL
        usb_hw_ep_in_stall(usb_if, 0);
    } else if ((setup->bmRequestType & 0x80) != 0 /*&& setup->wLength != 0*/) {
        // DATA IN
        status_out = 1;
        usb_hw_ep_in(usb_if, 0, ret, setup->wLength, wLength != setup->wLength);
    } else {
        // STATUS IN
        usb_hw_ep_in(usb_if, 0, 0, 0, true);
    }
    // Ready to receive another Setup packet
    usb_hw_ep_out(usb_if, 0, &usb->ep0.setup, 1, status_out, 0);
}
