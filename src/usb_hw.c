#include "stm32c0xx.h"
#include "semihosting.h"
#include "log.h"
#include "usb.h"

static void usb_reset()
{
    USB_DRD_FS->DADDR = USB_DADDR_EF_Msk;
    usb_hw_set_address(0);
    usb_hw_ep_init();
    usb_cdc_init();
    usb_reset_handler();
}

void usb_init()
{
    // Initialise USB in device mode
    // Interrupts: correct transfer, reset
    USB_DRD_FS->CNTR = USB_CNTR_CTRM_Msk | USB_CNTR_RESETM_Msk;
    usb_reset();
    NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

void usb_connect(bool enable)
{
    USB_DRD_FS->BCDR = enable ? USB_BCDR_DPPU_Msk : 0;
}

bool usb_is_connected()
{
    return !!(USB_DRD_FS->BCDR & USB_BCDR_DPPU_Msk);
}

void USB_DRD_FS_IRQHandler()
{
    uint32_t istr = USB_DRD_FS->ISTR;
    uint32_t clr = 0x00037f80;  // Avoid clear unhandled interrupts
    // log_push(LogUSB_Interrupt, istr);

    if (istr & USB_ISTR_SOF_Msk) {
        // Start of frame
        log_push(LogUSB_SOF, USB_DRD_FS->FNR);
        clr &= ~USB_ISTR_SOF_Msk;
    }

    if (istr & USB_ISTR_CTR_Msk) {
        // Transfer complete
        usb_hw_ep_ctr_irq(istr & 0x1f);
    }

    if (istr & USB_ISTR_RESET_Msk) {
        // USB reset
        usb_reset();
        clr &= ~USB_ISTR_RESET_Msk;
    }

    // Clear handled interrupts
    USB_DRD_FS->ISTR = clr;
}

void usb_process()
{
    usb_hw_ep_process();
}
