#include "stm32c0xx.h"
#include "dma.h"

#define DMA_CH(ch)      ((DMA_Channel_TypeDef *)(DMA1_Channel1_BASE + (DMA1_Channel2_BASE - DMA1_Channel1_BASE) * (ch)))
#define DMAMUX_CCR(ch)  (((DMAMUX_Channel_TypeDef *)(DMAMUX1_Channel0_BASE + (DMAMUX1_Channel1_BASE - DMAMUX1_Channel0_BASE) * (ch)))->CCR)
#define DMAMUX_RGCR(ch) (((DMAMUX_RequestGen_TypeDef *)(DMAMUX1_RequestGenerator0_BASE + (DMAMUX1_RequestGenerator1_BASE - DMAMUX1_RequestGenerator0_BASE) * (ch)))->CCR)

enum {
    DmaMuxReqUsart1Rx = 50,
    DmaMuxReqUsart1Tx = 51,
    DmaMuxReqUsart2Rx = 52,
    DmaMuxReqUsart2Tx = 53,
};

void dma_m2m_init()
{
}

void dma_uart1_init(void *tx_buf, uint16_t tx_len, void *rx_buf, uint16_t rx_len)
{
    // Clear interrupts
    DMA1->IFCR = (0x0f << (4 * DmaChUart1Tx)) | (0x0f << (4 * DmaChUart1Rx));

    // Set up RX channel
    // DMA channel
    DMA_CH(DmaChUart1Rx)->CCR = 0;
    DMA_CH(DmaChUart1Rx)->CNDTR = rx_len;
    DMA_CH(DmaChUart1Rx)->CPAR = (uint32_t)&USART1->RDR;
    DMA_CH(DmaChUart1Rx)->CMAR = (uint32_t)rx_buf;
    // DMAMUX request
    DMAMUX_CCR(DmaChUart1Rx) = DMAMUX_CxCR_EGE_Msk | (DmaMuxReqUsart1Rx << DMAMUX_CxCR_DMAREQ_ID_Pos);
    // Peripheral(8b)-to-memory(8b, inc), circular, low priority, enabled
    DMA_CH(DmaChUart1Rx)->CCR = DMA_CCR_MINC_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_EN_Msk;

    // Set up TX channel
    // DMA channel
    DMA_CH(DmaChUart1Tx)->CCR = 0;
    DMA_CH(DmaChUart1Tx)->CNDTR = tx_len;
    DMA_CH(DmaChUart1Tx)->CPAR = (uint32_t)&USART1->TDR;
    DMA_CH(DmaChUart1Tx)->CMAR = (uint32_t)tx_buf;
    // DMAMUX request
    DMAMUX_CCR(DmaChUart1Tx) = DMAMUX_CxCR_EGE_Msk | (DmaMuxReqUsart1Tx << DMAMUX_CxCR_DMAREQ_ID_Pos);
    // Memory(8b, inc)-to-peripheral(8b), circular, low priority, disabled
    DMA_CH(DmaChUart1Tx)->CCR = DMA_CCR_MINC_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_DIR_Msk;
}

uint16_t dma_uart1_rx_progress()
{
    return DMA_CH(DmaChUart1Rx)->CNDTR;
}
