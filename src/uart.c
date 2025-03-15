#include "stm32c0xx.h"
#include "macros.h"
#include "dma.h"
#include "uart.h"

#define FIFO_BUF_SIZE   512

enum {Tx = 0, Rx = 1};

static struct {
    uint8_t buf[FIFO_BUF_SIZE] ALIGNED(4);
    uint16_t ofs;
} uart_fifo[2] ALIGNED(4);

void uart_init()
{
    // Initialise UART1 for BT UART HCI interface
    USART1->CR1 = 0;
    // 1 stop bit
    USART1->CR2 = 0;
    // TXRX FIFO threshold 1/8, CTS enabled, RTS enabled, DMA enabled
    USART1->CR3 = USART_CR3_CTSE_Msk | USART_CR3_RTSE_Msk |
        USART_CR3_DMAR_Msk;
    // Baud rate 115200
    USART1->BRR = (48000000 + 115200 / 2) / 115200;
    USART1->GTPR = 0;
    USART1->RTOR = 0;
    USART1->ICR = 0xffffffff;
    // Prescaler div by 1
    USART1->PRESC = 0;

    // Initialise UART DMA
    dma_uart1_init(&uart_fifo[Tx].buf[0], FIFO_BUF_SIZE, &uart_fifo[Rx].buf[0], FIFO_BUF_SIZE);

    // FIFO mode enabled, 8n1, oversampling by 16
    // Enable UART
    USART1->CR1 = USART_CR1_FIFOEN_Msk |
        USART_CR1_TE_Msk | USART_CR1_RE_Msk | USART_CR1_UE_Msk;
}

bool uart_rx_available()
{
    return (FIFO_BUF_SIZE * 2 - dma_uart1_rx_progress() - uart_fifo[Rx].ofs) % FIFO_BUF_SIZE;
}

uint8_t uart_rx()
{
    uint16_t ofs = uart_fifo[Rx].ofs;
    uart_fifo[Rx].ofs = (ofs + 1) % FIFO_BUF_SIZE;
    return uart_fifo[Rx].buf[ofs];
}

bool uart_tx_free()
{
    return !!(USART1->ISR & USART_ISR_TXE_TXFNF_Msk);
}

void uart_tx(uint8_t v)
{
    USART1->TDR = v;
}
