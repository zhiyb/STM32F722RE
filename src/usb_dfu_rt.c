#include "bootloader.h"
#include "usb_dfu.h"

#ifdef BOOTLOADER
#error Only used in application
#endif

// #define REATTACH_DELAY_MS   100

typedef enum {
    REQ_CLASS_DFU_DETACH    = 0,
    REQ_CLASS_DFU_DNLOAD    = 1,
    REQ_CLASS_DFU_UPLOAD    = 2,
    REQ_CLASS_DFU_GETSTATUS = 3,
    REQ_CLASS_DFU_CLRSTATUS = 4,
    REQ_CLASS_DFU_GETSTATE  = 5,
    REQ_CLASS_DFU_ABORT     = 6,
} bRequest_t;

const void *usb_dfu_setup(setup_t *setup, void *data, uint32_t len)
{
    switch (setup->bRequest) {
    case USB_SETUP_REQ_SET_INTERFACE:
        // bmRequestType == 0x01
        return setup->wValue == 0 ? 0 : SETUP_STALL;
    }

    // In UsbDfuState_appIDLE state
    switch (setup->bRequest) {
    // case REQ_CLASS_DFU_GETSTATUS:
    //     return update_status(setup);
    // case REQ_CLASS_DFU_GETSTATE:
    //     return update_state(setup);

    case REQ_CLASS_DFU_DETACH:
        // bmRequestType == 0x21
        // usb_dfu.status.bState = UsbDfuState_appDETACH;
        for (uint32_t usb_if = 0; usb_if < UsbNumIfs; usb_if++)
            usb_connect(usb_if, false);
        bootloader_run_usb_dfu();
        return 0;
    }
    return SETUP_STALL;
}
