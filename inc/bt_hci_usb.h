#pragma once

#include "usb.h"

const void *bt_hci_usb_setup(setup_t *setup);

void bt_hci_usb_acl_confirm();
void bt_hci_usb_event_confirm();

bool bt_hci_usb_acl_tx(uint32_t *data, uint16_t len);

void bt_hci_usb_process();
