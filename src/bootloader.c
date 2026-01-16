#include "stm32f7xx.h"
#include "macros.h"
#include "flash.h"
#include "bootloader.h"
#include "semihosting.h"

#ifdef BOOTLOADER
#error Only used in application
#endif

extern void Reset_Handler();

static const firmware_header_t fw_header USED SECTION(.fw_header) = {
    .header_size = 4 * 3,
    .version = -1,
    .entry = &Reset_Handler,
    .ext_tag = {0},
};

void bootloader_run_usb_dfu()
{
    // Configure bootloader USB DFU mode request then reset
    bootloader_req->op = BootloaderUsbDfu;
    bootloader_req->param = 0;
    NVIC_SystemReset();
}
