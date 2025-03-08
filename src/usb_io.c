#include "stm32c071xx.h"
#include "usb.h"

void usb_init()
{
    // Initialise USB in device mode
    USB_DRD_FS->CNTR = 0;
    // USB_DRD_FS->DADDR = 0;
    USB_DRD_FS->DADDR = USB_DADDR_EF_Msk;
}

void usb_connect(bool enable)
{
    USB_DRD_FS->BCDR = enable ? USB_BCDR_DPPU_Msk : 0;
}

bool usb_is_connected()
{
    return !!(USB_DRD_FS->BCDR & USB_BCDR_DPPU_Msk);
}
