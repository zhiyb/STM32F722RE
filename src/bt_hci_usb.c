#include "bt_hci_usb.h"
#include "bt_hci_h4.h"
#include "usb.h"
#include "semihosting.h"

const void *bt_hci_usb_setup(setup_t *setup)
{
    // Treat the packet as an HCI command packet regardless of
    // the value of bRequest, wValue and wIndex
    uint8_t *data = setup->data;
    uint8_t len = setup->wLength;
    bt_hci_h4_command(data, len);
    return 0;
}

void bt_hci_usb_event(uint8_t *data, uint8_t len)
{
    usb_hw_ep_tx(UsbEpBtHciEvents, data, len, false);
}
