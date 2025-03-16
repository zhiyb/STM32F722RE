#pragma once

#include <stdbool.h>
#include <stdint.h>

void uart_init();
bool uart_rx_available();
uint8_t uart_rx();
bool uart_tx_free();
void uart_tx(uint8_t v);
uint8_t uart_dma_irq(uint8_t isr);
