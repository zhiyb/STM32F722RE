#pragma once

#include <stdint.h>

void bt_hci_h4_reset();

// Upstream
void bt_hci_h4_command(const uint8_t *data, uint8_t len);

// Downstream
void bt_hci_h4_rx(uint8_t v);
