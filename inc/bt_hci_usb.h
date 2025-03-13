#pragma once

#include "usb.h"

const void *bt_hci_usb_setup(setup_t *setup);
void bt_hci_usb_event(uint8_t *data, uint8_t len);
