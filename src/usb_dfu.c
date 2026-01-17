#include "semihosting.h"
#include "macros.h"
#include "flash.h"
#include "usb_dfu.h"
#include "bootloader.h"
#include "log.h"

#ifndef BOOTLOADER
#error Only used in USB DFU bootloader
#endif

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
    uint8_t bStatus;            // usb_dfu_bStatus_t
    uint8_t bwPollTimeout[3];
    uint8_t bState;             // usb_dfu_bState_t
    uint8_t iString;
} status_t;

static struct {
    status_t status ALIGNED(4);
    struct PACKED {
        uint8_t bState;         // usb_dfu_bState_t
    } state ALIGNED(4);
} usb_dfu;

static const void *update_status(setup_t *setup)
{
    // bmRequestType == 0xa1
    status_t *status = &usb_dfu.status;
    status->bStatus = usb_dfu.status.bStatus;
    status->bwPollTimeout[0] = 0;
    status->bwPollTimeout[1] = 0;
    status->bwPollTimeout[2] = 0;
    status->bState = usb_dfu.status.bState;
    status->iString = 0;
    setup->wLength = MIN(setup->wLength, 6);
    return status;
}

static const void *update_state(setup_t *setup)
{
    usb_dfu.state.bState = usb_dfu.status.bState;
    setup->wLength = MIN(setup->wLength, 1);
    return &usb_dfu.state;
}

void usb_dfu_usb_reset()
{
    // If host hasn't requested any DFU operations yet, wait
    if (usb_dfu.status.bState < UsbDfuState_dfuIDLE)
        return;
    // Reset USB DFU state?
    // usb_dfu.status.bState = UsbDfuState_dfuIDLE;
    // If application firmware is valid, reset to application
    if (firmware_header->header_size != 0xffffffff)
        bootloader_run_fw();
}

static usb_dfu_bStatus_t flash_error(flash_state_t fstate)
{
    switch (fstate) {
    case FlashUnlockError:
        return UsbDfuStatus_errWRITE;
    case FlashEraseError:
        return UsbDfuStatus_errERASE;
    case FlashProgramError:
    case FlashProgramHeaderError:
        return UsbDfuStatus_errPROG;
    case FlashVerifyError:
    case FlashVerifyHeaderError:
        return UsbDfuStatus_errVERIFY;
    }
    return UsbDfuStatus_errUNKNOWN;
}

const void *usb_dfu_setup(setup_t *setup, void *data, uint32_t len)
{
    switch (setup->bRequest) {
    case USB_SETUP_REQ_SET_INTERFACE:
        // bmRequestType == 0x01
        return setup->wValue == 0 ? 0 : SETUP_STALL;
    }

    const void *p;
    flash_state_t fstate;
    switch ((usb_dfu_bState_t)usb_dfu.status.bState) {
    case UsbDfuState_appIDLE:
    case UsbDfuState_appDETACH:
        // Just in case
        usb_dfu.status.bState = UsbDfuState_dfuIDLE;
        // fall-through
    case UsbDfuState_dfuIDLE:
        switch (setup->bRequest) {
        case REQ_CLASS_DFU_GETSTATUS:
            return update_status(setup);
        case REQ_CLASS_DFU_GETSTATE:
            return update_state(setup);
        case REQ_CLASS_DFU_ABORT:
            return 0;
        case REQ_CLASS_DFU_DETACH:
            // Technically not in standard
            // dfu-util attempts this before resetting to application firmware
            usb_dfu_usb_reset();
            return 0;

        case REQ_CLASS_DFU_DNLOAD:
            if (len == USB_DFU_TRANSFER_SIZE) {
                log_push(LogUsbDfu_Download, len);
                flash_uf2_write_init();
                if (!flash_uf2_write_block(data)) {
                    usb_dfu.status.bStatus = UsbDfuStatus_errTARGET;
                    usb_dfu.status.bState = UsbDfuState_dfuERROR;
                    return SETUP_STALL;
                }
                usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_SYNC;
                return 0;
            }
            break;

        case REQ_CLASS_DFU_UPLOAD:
            // bmRequestType == 0xa1
            if (setup->wLength == USB_DFU_TRANSFER_SIZE) {
                p = flash_uf2_read_block(setup->wValue);
                if (!p) {
                    setup->wLength = 0;
                    usb_dfu.status.bState = UsbDfuState_dfuIDLE;
                    return 0;
                } else {
                    usb_dfu.status.bState = UsbDfuState_dfuUPLOAD_IDLE;
                    return p;
                }
            }
            break;
        }
        break;

    case UsbDfuState_dfuDNLOAD_SYNC:
    // As this DFU firmware can still respond to USB communication,
    // dfuDNBUSY is effectively same as dfuDNLOAD_SYNC
    case UsbDfuState_dfuDNBUSY:
        switch (setup->bRequest) {
        case REQ_CLASS_DFU_GETSTATUS:
            fstate = flash_state();
            if (fstate >= FlashError) {
                usb_dfu.status.bStatus = flash_error(fstate);
                usb_dfu.status.bState = UsbDfuState_dfuERROR;
            } else if (fstate != FlashIdle) {
                usb_dfu.status.bState = UsbDfuState_dfuDNBUSY;
            } else {
                usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_IDLE;
            }
            return update_status(setup);
        case REQ_CLASS_DFU_GETSTATE:
            usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_SYNC;
            return update_state(setup);
        }
        break;

    case UsbDfuState_dfuDNLOAD_IDLE:
        switch (setup->bRequest) {
        case REQ_CLASS_DFU_GETSTATUS:
            return update_status(setup);
        case REQ_CLASS_DFU_GETSTATE:
            return update_state(setup);
        case REQ_CLASS_DFU_ABORT:
            usb_dfu.status.bState = UsbDfuState_dfuIDLE;
            flash_uf2_write_abort();
            return 0;

        case REQ_CLASS_DFU_DNLOAD:
            if (len == USB_DFU_TRANSFER_SIZE) {
                log_push(LogUsbDfu_Download, len);
                if (!flash_uf2_write_block(data)) {
                    usb_dfu.status.bStatus = UsbDfuStatus_errTARGET;
                    usb_dfu.status.bState = UsbDfuState_dfuERROR;
                    return SETUP_STALL;
                }
                usb_dfu.status.bState = UsbDfuState_dfuDNLOAD_SYNC;
                return 0;

            } else if (len == 0) {
                // No more data to download
                log_push(LogUsbDfu_Manifest, len);
                if (!flash_uf2_write_finish()) {
                    usb_dfu.status.bStatus = UsbDfuStatus_errNOTDONE;
                    usb_dfu.status.bState = UsbDfuState_dfuERROR;
                    return SETUP_STALL;
                }
                usb_dfu.status.bState = UsbDfuState_dfuMANIFEST_SYNC;
                return 0;
            }
            break;
        }
        break;

    case UsbDfuState_dfuMANIFEST_SYNC:
    // As this DFU firmware can still respond to USB communication,
    // dfuMANIFEST is effectively same as dfuMANIFEST_SYNC
    case UsbDfuState_dfuMANIFEST:
    case UsbDfuState_dfuMANIFEST_WAIT_RESET:    // Should not be possible
        switch (setup->bRequest) {
        case REQ_CLASS_DFU_GETSTATUS:
            fstate = flash_state();
            if (fstate >= FlashError) {
                usb_dfu.status.bStatus = flash_error(fstate);
                usb_dfu.status.bState = UsbDfuState_dfuERROR;
            } else if (fstate != FlashIdle) {
                usb_dfu.status.bState = UsbDfuState_dfuMANIFEST;
            } else {
                usb_dfu.status.bState = UsbDfuState_dfuIDLE;
            }
            return update_status(setup);
        case REQ_CLASS_DFU_GETSTATE:
            usb_dfu.status.bState = UsbDfuState_dfuMANIFEST_SYNC;
            return update_state(setup);
        }
        break;

    case UsbDfuState_dfuUPLOAD_IDLE:
        switch (setup->bRequest) {
        case REQ_CLASS_DFU_GETSTATUS:
            return update_status(setup);
        case REQ_CLASS_DFU_GETSTATE:
            return update_state(setup);
        case REQ_CLASS_DFU_ABORT:
            usb_dfu.status.bState = UsbDfuState_dfuIDLE;
            return 0;

        case REQ_CLASS_DFU_UPLOAD:
            // bmRequestType == 0xa1
            if (setup->wLength == USB_DFU_TRANSFER_SIZE) {
                p = flash_uf2_read_block(setup->wValue);
                if (!p) {
                    setup->wLength = 0;
                    usb_dfu.status.bState = UsbDfuState_dfuIDLE;
                    return 0;
                } else {
                    usb_dfu.status.bState = UsbDfuState_dfuUPLOAD_IDLE;
                    return p;
                }
            }
            break;
        }
        break;

    case UsbDfuState_dfuERROR:
        switch (setup->bRequest) {
        case REQ_CLASS_DFU_GETSTATUS:
            return update_status(setup);
        case REQ_CLASS_DFU_GETSTATE:
            return update_state(setup);
        case REQ_CLASS_DFU_CLRSTATUS:
            // Wait for flash operations to finish
            flash_uf2_write_abort();
            usb_dfu.status.bStatus = UsbDfuStatus_OK;
            usb_dfu.status.bState = UsbDfuState_dfuIDLE;
            return 0;
        }
        break;
    }

    DBG_BKPT("Unknown request");
    usb_dfu.status.bStatus = UsbDfuStatus_errSTALLEDPKT;
    usb_dfu.status.bState = UsbDfuState_dfuERROR;
    return SETUP_STALL;
}

usb_dfu_bState_t usb_dfu_state()
{
    return usb_dfu.status.bState;
}
