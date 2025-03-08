#include "usb.h"
#include "stm32c0xx.h"
#include "semihosting.h"

void usb_init()
{
    // Initialise USB in device mode
    // Interrupts: correct transfer, reset
    USB_DRD_FS->CNTR = USB_CNTR_CTRM_Msk | USB_CNTR_RESETM_Msk;
    // USB_DRD_FS->DADDR = 0;
    USB_DRD_FS->DADDR = USB_DADDR_EF_Msk;

    // usb_buf_init_rx(0);
    usb_hw_buf_init();
    usb_hw_ep0_init();

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

    if (istr & USB_ISTR_RESET_Msk) {
        // USB reset
        clr &= ~USB_ISTR_RESET_Msk;
        usb_hw_buf_init();
        usb_hw_ep0_init();

    } else if (istr & USB_ISTR_CTR_Msk) {
        // Transfer complete
        usb_hw_ep_ctr_irq();
    }

    // Clear handled interrupts
    USB_DRD_FS->ISTR = clr;
}

void usb_process()
{
    usb_hw_ep_process();
}
