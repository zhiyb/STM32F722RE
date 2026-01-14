#include "stm32f7xx.h"
#include "semihosting.h"
#include "bootloader.h"
#include "macros.h"

static void boot_fw()
{
    if (firmware_header->header_size != 0xffffffff)
        firmware_header->entry();
}

static void boot_run()
{
    // Copy USB bootloader to ITCM RAM
    static const uint8_t fw_bin[] ALIGNED(4) = {
#include "bl_usb.h"
    };
    volatile uint32_t *dst = (uint32_t *)&__bootloader_req;
    const uint32_t *src = (const uint32_t *)fw_bin;
    for (uint32_t i = 0; i < (sizeof(fw_bin) + 3) / 4; i++)
        dst[i] = src[i];

    // Check if bootloader is valid
    bootloader_op_t op = (bootloader_op_t)bootloader_req->op;
    bootloader_req->op = BootloaderBootFw;
    if (op != BootloaderRunItcm)
        return;

    // Find entry point and run it
    void (*entry)() = (void (*)())bootloader_req->ptr;
    entry();
}

static void boot_run_direct()
{
    void (*entry)() = (void (*)())bootloader_req->ptr;
    entry();
}

static void reset()
{
    __NVIC_SystemReset();
}

static void bootloader_systick_init_hsi()
{
    // HSI runs at 16 MHz
    const uint32_t period = 16 * 1000 / 8;
    // Configure SysTick to 1ms period
    SysTick->CTRL = 0;
    // SysTick->LOAD = SysTick->CALIB;
    SysTick->LOAD = period - 1;
    SysTick->VAL = 0;
    // SysTick interrupt enable not controlled by NVIC
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
}

static void bootloader_systick_delay_ms(uint32_t ms)
{
    while (ms--)
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
}

void Reset_Handler()
{
    // Check reset reason
    uint32_t csr = RCC->CSR;
    RCC->CSR = csr | RCC_CSR_RMVF_Msk;

    if (csr & RCC_CSR_SFTRSTF_Msk) {
        // Software reset, check requested operation
        bootloader_op_t op = (bootloader_op_t)bootloader_req->op;
        bootloader_req->op = BootloaderBootFw;
        if (op == BootloaderRunItcm) {
            // Possibly debugger reset, jump to ITCM directly
            boot_run_direct();
        } else if (op == BootloaderRun || op == BootloaderUsbDfu) {
            goto run_bootloader;
        }

    } else if (csr & RCC_CSR_PINRSTF_Msk) {
        // Reset button
        bootloader_op_t op = (bootloader_op_t)bootloader_req->op;
        bootloader_req->op = BootloaderButton;
        if (op == BootloaderButton) {
            // Reset button pressed again quickly, start bootloader
            bootloader_req->op = BootloaderBootFw;
            goto run_bootloader;
        }

        // Wait 200ms for key press to start USB bootloader
        // HSI is used after reset
        bootloader_systick_init_hsi(16);
        bootloader_systick_delay_ms(200);

        // Timer expired, boot firmware normally
        bootloader_req->op = BootloaderBootFw;
        reset();
    }

    boot_fw();
run_bootloader:
    boot_run();
    for (;;)
        reset();
}
