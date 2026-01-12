#include "semihosting.h"
#include "macros.h"
#include "systick.h"
#include "flash.h"
#include "usb.h"
#include "usb_internal.h"
#include "usb_dfu.h"

#define REATTACH_DELAY_MS   500

typedef enum {
    REQ_CLASS_DFU_DETACH    = 0,
    REQ_CLASS_DFU_DNLOAD    = 1,
    REQ_CLASS_DFU_UPLOAD    = 2,
    REQ_CLASS_DFU_GETSTATUS = 3,
    REQ_CLASS_DFU_CLRSTATUS = 4,
    REQ_CLASS_DFU_GETSTATE  = 5,
    REQ_CLASS_DFU_ABORT     = 6,
} bRequest_t;

typedef struct PACKED {
    uint8_t bStatus;
    uint8_t bwPollTimeout[3];
    uint8_t bState;
    uint8_t iString;
} status_t;

static struct {
    status_t status ALIGNED(4);
    uint32_t ms;
    bool pending;
} usb_dfu;

const void *usb_dfu_setup(usb_dfu_t *dfu, setup_t *setup)
{
    status_t *status = &usb_dfu.status;
    switch (setup->bRequest) {
    case USB_SETUP_REQ_SET_INTERFACE:
        // bmRequestType == 0x01
        return setup->wValue == 0 ? 0 : SETUP_STALL;

    case REQ_CLASS_DFU_GETSTATUS:
        // bmRequestType == 0xa1
        status->bStatus = usb_dfu.status.bStatus;
        status->bwPollTimeout[0] = 0;
        status->bwPollTimeout[1] = 0;
        status->bwPollTimeout[2] = 0;
        status->bState = usb_dfu.status.bState;
        status->iString = 0;
        setup->wLength = MIN(setup->wLength, 6);
        return status;

    case REQ_CLASS_DFU_DETACH:
        // bmRequestType == 0x21
        if (usb_dfu.status.bState != UsbDfuState_appIDLE)
            return SETUP_STALL;
        usb_dfu.ms = systick_ms();
        usb_dfu.status.bState = UsbDfuState_appDETACH;
        usb_dfu.pending = true;
        for (uint32_t usb_if = 0; usb_if < NumUsbIfs; usb_if++)
            usb_connect(usb_if, false);
        break;

    case REQ_CLASS_DFU_UPLOAD: {
        // bmRequestType == 0xa1
        if (usb_dfu.status.bState != UsbDfuState_dfuIDLE &&
            usb_dfu.status.bState != UsbDfuState_dfuUPLOAD_IDLE)
            return SETUP_STALL;
        usb_dfu.status.bState = UsbDfuState_dfuUPLOAD_IDLE;
        const void *p = flash_uf2_read_block(setup->wValue);
        if (!p) {
            setup->wLength = 0;
            return 0;
        } else {
            setup->wLength = MIN(setup->wLength, 512);
            return p;
        }
        break;
    }

    default:
        DBG_BKPT("Unknown request");
        return SETUP_STALL;
    }
}

usb_dfu_bState_t usb_dfu_state()
{
    return usb_dfu.status.bState;
}

void usb_dfu_process()
{
    if (!usb_dfu.pending)
        return;

    switch (usb_dfu.status.bState) {
    case UsbDfuState_appDETACH:
        if (systick_ms() - usb_dfu.ms >= REATTACH_DELAY_MS) {
            usb_dfu.pending = false;
            usb_dfu.status.bState = UsbDfuState_dfuIDLE;
            for (uint32_t usb_if = 0; usb_if < NumUsbIfs; usb_if++)
                usb_connect(usb_if, true);
        }
        break;

    default:
        TODO();
        break;
    }
}
