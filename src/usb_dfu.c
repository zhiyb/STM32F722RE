#include "semihosting.h"
#include "macros.h"
#include "systick.h"
#include "flash.h"
#include "usb.h"
#include "usb_internal.h"
#include "usb_dfu.h"
#include "log.h"

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

const void *usb_dfu_setup(setup_t *setup, void *data, uint32_t len)
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
            usb_dfu.status.bState = UsbDfuState_dfuIDLE;
            return 0;
        } else {
            setup->wLength = MIN(setup->wLength, 512);
            return p;
        }
        break;
    }

    case REQ_CLASS_DFU_DNLOAD:
        switch (usb_dfu.status.bState) {
        case UsbDfuState_dfuIDLE:
            if (len != 0) {
                log_push(LogUsbDfu_Download, len);
                usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_SYNC;
                usb_dfu.pending = true;
                return 0;
            }
            break;
        case UsbDfuState_dfuDNLOAD_IDLE:
            log_push(LogUsbDfu_Download, len);
            if (len != 0) {
                usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_SYNC;
                usb_dfu.pending = true;
                return 0;
            } else if (true) {
                // Data transfer completed and OK
                usb_dfu.status.bState = UsbDfuState_dfuMANIFEST_SYNC;
                usb_dfu.pending = true;
                return 0;
            } else {
                // Data transfer completed but NOT OK
            }
            break;
        }
        usb_dfu.status.bState = UsbDfuState_dfuERROR;
        usb_dfu.status.bStatus = UsbDfuStatus_errUNKNOWN;
        return SETUP_STALL;

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

    case UsbDfuState_dfuDNLOAD_SYNC:
        // TODO download data to flash
        log_push(LogUsbDfu_Proc_Download, usb_dfu.status.bState);
        usb_dfu.pending = false;
        usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_IDLE;
        break;

    case UsbDfuState_dfuMANIFEST_SYNC:
        // TODO data complete
        log_push(LogUsbDfu_Proc_Manifest, usb_dfu.status.bState);
        usb_dfu.pending = false;
        usb_dfu.status.bState = UsbDfuState_dfuIDLE;
        break;

    default:
        TODO();
        break;
    }
}
