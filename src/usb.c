#include "usb.h"
#include "usb_internal.h"

usb_t usb_ifs[NumUsbIfs];

void usb_init(usb_if_t usb_if)
{
    usb_hw_init(usb_if);
}

void usb_connect(usb_if_t usb_if, bool enable)
{
    usb_hw_connect(usb_if, enable);
}

void usb_process(usb_if_t usb_if)
{
    usb_hw_process(usb_if);
}
