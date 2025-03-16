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
    bt_hci_h4_tx(BtH4HciCommand, data, len);
    return 0;
}

bool bt_hci_usb_acl_tx(uint32_t *data, uint16_t len)
{
    dbg_bkpt();
    bt_hci_h4_tx(BtH4HciAclData, data, len);
    return true;
}

void bt_hci_usb_acl_confirm()
{
    bt_hci_h4_confirm(BtH4HciAclData);
}

void bt_hci_usb_event_confirm()
{
    bt_hci_h4_confirm(BtH4HciEvent);
}

#include "systick.h"

void bt_hci_usb_process()
{
    if (usb_hw_ep_tx_db_available(UsbEpBtACLDataIn)) {
        // Check for available ACL data packet
        uint8_t len = 0;
        const uint8_t *data = bt_hci_h4_read(BtH4HciAclData, &len);
        if (data)
            usb_hw_ep_tx_db(UsbEpBtACLDataIn, data, len);
    }

    if (usb_hw_ep_tx_status(UsbEpBtHciEvents) != UsbEpValid) {
        // Check for available Event packet
        uint8_t len = 0;
        const uint8_t *data = bt_hci_h4_read(BtH4HciEvent, &len);
        if (data)
            usb_hw_ep_tx(UsbEpBtHciEvents, data, len, false);
    }
}
