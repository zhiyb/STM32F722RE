#include "stm32c0xx.h"
#include "macros.h"
#include "dma.h"
#include "semihosting.h"
#include "uart.h"

#define BAUD_RATE       1000000

#define FIFO_BUF_SIZE   4096

enum {Tx = 0, Rx = 1};

static struct {
    uint8_t buf[FIFO_BUF_SIZE] ALIGNED(4);
    volatile uint16_t rptr;
} uart_fifo[2];

static inline void uart_dma_init()
{
    // Clear interrupts
    DMA1->IFCR = (0x0f << (4 * DmaChUart1Tx)) | (0x0f << (4 * DmaChUart1Rx));

    // Set up RX channel
    // DMA channel
    uart_fifo[Rx].rptr = 0;
    DMA_CH(DmaChUart1Rx)->CCR = 0;
    DMA_CH(DmaChUart1Rx)->CNDTR = FIFO_BUF_SIZE;
    DMA_CH(DmaChUart1Rx)->CPAR = (uint32_t)&USART1->RDR;
    DMA_CH(DmaChUart1Rx)->CMAR = (uint32_t)&uart_fifo[Rx].buf[0];
    // DMAMUX request
    DMAMUX_CCR(DmaChUart1Rx) = DMAMUX_CxCR_EGE_Msk | (DmaMuxReqUsart1Rx << DMAMUX_CxCR_DMAREQ_ID_Pos);
    // Peripheral(8b)-to-memory(8b, inc), circular, low priority, enabled
    // Half and complete transfer interrupts enabled
    DMA_CH(DmaChUart1Rx)->CCR = DMA_CCR_MINC_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_EN_Msk |
        DMA_CCR_HTIE_Msk | DMA_CCR_TCIE_Msk;

    // Set up TX channel
    // DMA channel
    DMA_CH(DmaChUart1Tx)->CCR = 0;
    DMA_CH(DmaChUart1Tx)->CNDTR = FIFO_BUF_SIZE;
    DMA_CH(DmaChUart1Tx)->CPAR = (uint32_t)&USART1->TDR;
    DMA_CH(DmaChUart1Tx)->CMAR = (uint32_t)&uart_fifo[Tx].buf[0];
    // DMAMUX request
    DMAMUX_CCR(DmaChUart1Tx) = DMAMUX_CxCR_EGE_Msk | (DmaMuxReqUsart1Tx << DMAMUX_CxCR_DMAREQ_ID_Pos);
    // Memory(8b, inc)-to-peripheral(8b), circular, low priority, disabled
    DMA_CH(DmaChUart1Tx)->CCR = DMA_CCR_MINC_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_DIR_Msk;
}

void uart_init()
{
    // Initialise UART1 for BT UART HCI interface
    USART1->CR1 = 0;
    // 1 stop bit
    USART1->CR2 = 0;
    // TXRX FIFO threshold 1/8, CTS enabled, RTS enabled, DMA enabled
    USART1->CR3 = USART_CR3_CTSE_Msk | USART_CR3_RTSE_Msk |
        USART_CR3_DMAR_Msk;
    USART1->BRR = (48000000 + BAUD_RATE / 2) / BAUD_RATE;
    USART1->GTPR = 0;
    USART1->RTOR = 0;
    USART1->ICR = 0xffffffff;
    // Prescaler div by 1
    USART1->PRESC = 0;

    // Initialise UART DMA
    uart_dma_init();

    // FIFO mode enabled, 8n1, oversampling by 16
    // Enable UART
    USART1->CR1 = USART_CR1_FIFOEN_Msk |
        USART_CR1_TE_Msk | USART_CR1_RE_Msk | USART_CR1_UE_Msk;
}

bool uart_rx_available()
{
    uint16_t dma_cndtr = DMA_CH(DmaChUart1Rx)->CNDTR;
    return (FIFO_BUF_SIZE * 2 - dma_cndtr - uart_fifo[Rx].rptr) % FIFO_BUF_SIZE;
}

uint8_t uart_rx()
{
    uint16_t ofs = uart_fifo[Rx].rptr;
    uart_fifo[Rx].rptr = (ofs + 1) % FIFO_BUF_SIZE;
    return uart_fifo[Rx].buf[ofs];
}

uint8_t uart_dma_irq(uint8_t isr)
{
    if ((isr & (DMA_ISR_HTIF1_Msk | DMA_ISR_TCIF1_Msk)) == (DMA_ISR_HTIF1_Msk | DMA_ISR_TCIF1_Msk)) {
        // Half-transfer and full-transfer complete, overrun
        DBG_BKPT("DMA overrun");
    }

    bool pause = false;

    // Half Transfer complete
    if ((isr & DMA_ISR_HTIF1_Msk) && (uart_fifo[Rx].rptr >= FIFO_BUF_SIZE / 2))
        pause = true;
    // Full Transfer complete
    if ((isr & DMA_ISR_TCIF1_Msk) && (uart_fifo[Rx].rptr < FIFO_BUF_SIZE / 2))
        pause = true;

    if (pause) {
        // Disable DMA peripheral request to avoid overrun
        // TODO: Recovery
        DMAMUX_CCR(DmaChUart1Rx) = 0;
        dbg_bkpt();
    }
    return isr;
}

bool uart_tx_free()
{
    return !!(USART1->ISR & USART_ISR_TXE_TXFNF_Msk);
}

void uart_tx(uint8_t v)
{
    USART1->TDR = v;
}
