#pragma once

#include <stdint.h>

#define DMA_CH(ch)      ((DMA_Channel_TypeDef *)(DMA1_Channel1_BASE + (DMA1_Channel2_BASE - DMA1_Channel1_BASE) * (ch)))
#define DMAMUX_CCR(ch)  (((DMAMUX_Channel_TypeDef *)(DMAMUX1_Channel0_BASE + (DMAMUX1_Channel1_BASE - DMAMUX1_Channel0_BASE) * (ch)))->CCR)
#define DMAMUX_RGCR(ch) (((DMAMUX_RequestGen_TypeDef *)(DMAMUX1_RequestGenerator0_BASE + (DMAMUX1_RequestGenerator1_BASE - DMAMUX1_RequestGenerator0_BASE) * (ch)))->CCR)

typedef enum {
    DmaMuxReqUsart1Rx = 50,
    DmaMuxReqUsart1Tx = 51,
    DmaMuxReqUsart2Rx = 52,
    DmaMuxReqUsart2Tx = 53,
} dma_mux_req_t;

typedef enum {
    DmaChUart1Rx,
    DmaChUart1Tx,
    DmaChM2M,
} dma_channel_t;

void dma_init();

void dma_m2m_init();

void dma_uart1_init(void *tx_buf, uint16_t tx_len, void *rx_buf, uint16_t rx_len);
uint16_t dma_uart1_rx_progress();
