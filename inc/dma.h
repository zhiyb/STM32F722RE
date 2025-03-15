#pragma once

#include <stdint.h>

typedef enum {
    DmaChUart1Tx,
    DmaChUart1Rx,
    DmaChM2M,
} dma_channel_t;

void dma_m2m_init();

void dma_uart1_init(void *tx_buf, uint16_t tx_len, void *rx_buf, uint16_t rx_len);
uint16_t dma_uart1_rx_progress();
