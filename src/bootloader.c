#include "stm32f7xx.h"
#include "bootloader.h"
#include "macros.h"

#ifndef BOOTLOADER
extern void Reset_Handler();
static const firmware_header_t fw_header USED SECTION(.fw_header) = {
    .header_size = 4 * 3,
    .version = -1,
    .entry = &Reset_Handler,
    .ext_tag = {0},
};
#endif

void bootloader_run_fw()
{
    // Configure bootloader application firmware request then reset
    bootloader_req->op = BootloaderBootFw;
    bootloader_req->param = 0;
    NVIC_SystemReset();
}

void bootloader_run_usb_dfu()
{
    // Configure bootloader USB DFU mode request then reset
    bootloader_req->op = BootloaderUsbDfu;
    bootloader_req->param = 0;
    NVIC_SystemReset();
}
