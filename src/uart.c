#include "stm32c0xx.h"
#include "uart.h"

void uart_init()
{
    // Initialise UART1 for BT UART HCI interface
    USART1->CR1 = 0;
    // 1 stop bit
    USART1->CR2 = 0;
    // TXRX FIFO threshold 1/8, CTS enabled, RTS enabled, DMA disabled
    USART1->CR3 = USART_CR3_CTSE_Msk | USART_CR3_RTSE_Msk;
    // Baud rate 115200
    USART1->BRR = (48000000 + 115200 / 2) / 115200;
    USART1->GTPR = 0;
    USART1->RTOR = 0;
    USART1->ICR = 0xffffffff;
    // Prescaler div by 1
    USART1->PRESC = 0;
    // FIFO mode enabled, 8n1, oversampling by 16
    // Enable UART
    USART1->CR1 = USART_CR1_FIFOEN_Msk |
        USART_CR1_TE_Msk | USART_CR1_RE_Msk | USART_CR1_UE_Msk;
}

bool uart_rx_available()
{
    return !!(USART1->ISR & USART_ISR_RXNE_RXFNE_Msk);
}

uint8_t uart_rx()
{
    return USART1->RDR;
}

bool uart_tx_free()
{
    return !!(USART1->ISR & USART_ISR_TXE_TXFNF_Msk);
}

void uart_tx(uint8_t v)
{
    USART1->TDR = v;
}
