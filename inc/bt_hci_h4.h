#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BtH4HciCommand = 0x01,
    BtH4HciAclData = 0x02,
    BtH4HciSyncData = 0x03,
    BtH4HciEvent = 0x04,
} bt_h4_type_t;

void bt_hci_h4_reset();
void bt_hci_h4_process();

// Upstream
void bt_hci_h4_tx(bt_h4_type_t type, const uint8_t *data, uint8_t len);
bool bt_hci_h4_available(bt_h4_type_t type);
const uint8_t *bt_hci_h4_read(bt_h4_type_t type, uint8_t *len);
void bt_hci_h4_confirm(bt_h4_type_t type);

// Downstream
void bt_hci_h4_rx(uint8_t v);
