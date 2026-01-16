#pragma once
#include "usb.h"
#include "flash.h"

#define USB_DFU_TRANSFER_SIZE   FLASH_UF2_BLOCK_SIZE

typedef enum {
    UsbDfuStatus_OK = 0,            // No error condition is present.
    UsbDfuStatus_errTARGET,         // File is not targeted for use by this device.
    UsbDfuStatus_errFILE,           // File is for this device but fails some vendor-specific verification test.
    UsbDfuStatus_errWRITE,          // Device is unable to write memory.
    UsbDfuStatus_errERASE,          // Memory erase function failed.
    UsbDfuStatus_errCHECK_ERASED,   // Memory erase check failed.
    UsbDfuStatus_errPROG,           // Program memory function failed.
    UsbDfuStatus_errVERIFY,         // Programmed memory failed verification.
    UsbDfuStatus_errADDRESS,        // Cannot program memory due to received address that is out of range.
    UsbDfuStatus_errNOTDONE,        // Received DFU_DNLOAD with wLength = 0, but device does not think it has all of the data yet.
    UsbDfuStatus_errFIRMWARE,       // Device's firmware is corrupt. It cannot return to run-time (non-DFU) operations.
    UsbDfuStatus_errVENDOR,         // iString indicates a vendor-specific error.
    UsbDfuStatus_errUSBR,           // Device detected unexpected USB reset signaling.
    UsbDfuStatus_errPOR,            // Device detected unexpected power on reset.
    UsbDfuStatus_errUNKNOWN,        // Something went wrong, but the device does not know what it was.
    UsbDfuStatus_errSTALLEDPKT,     // Device stalled an unexpected request.
} usb_dfu_bStatus_t;

typedef enum {
    // Device is running its normal application.
    UsbDfuState_appIDLE,
    // Device is running its normal application, has received the
    // DFU_DETACH request, and is waiting for a USB reset.
    UsbDfuState_appDETACH,
    // Device is operating in the DFU mode and is waiting for requests.
    UsbDfuState_dfuIDLE,
    // Device has received a block and is waiting for the host to solicit the status via DFU_GETSTATUS.
    UsbDfuState_dfuDNLOAD_SYNC,
    // Device is programming a control-write block into its nonvolatile memories.
    UsbDfuState_dfuDNBUSY,
    // Device is processing a download operation. Expecting DFU_DNLOAD requests.
    UsbDfuState_dfuDNLOAD_IDLE,
    // Device has received the final block of firmware from the host
    // and is waiting for receipt of DFU_GETSTATUS to begin the Manifestation phase;
    // or device has completed the Manifestation phase and is waiting for receipt of DFU_GETSTATUS.
    // (Devices that can enter this state after the Manifestation phase
    // set bmAttributes bit bitManifestationTolerant to 1.)
    UsbDfuState_dfuMANIFEST_SYNC,
    // Device is in the Manifestation phase.
    // (Not all devices will be able to respond to DFU_GETSTATUS when in this state.)
    UsbDfuState_dfuMANIFEST,
    // Device has programmed its memories and is waiting for a USB reset or a power on reset.
    // (Devices that must enter this state clear bitManifestationTolerant to 0.)
    UsbDfuState_dfuMANIFEST_WAIT_RESET,
    // The device is processing an upload operation. Expecting DFU_UPLOAD requests.
    UsbDfuState_dfuUPLOAD_IDLE,
    // An error has occurred. Awaiting the DFU_CLRSTATUS request.
    UsbDfuState_dfuERROR,
} usb_dfu_bState_t;

#ifdef BOOTLOADER
void usb_dfu_usb_reset();
#endif

const void *usb_dfu_setup(setup_t *setup, void *data, uint32_t len);
