#include <stdbool.h>
#include "stm32c0xx.h"
#include "semihosting.h"
#include "uart.h"
#include "dma.h"

void dma_init()
{
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void dma_m2m_init()
{
}

static inline uint32_t dma_irq(const uint32_t isr, const uint8_t ch)
{
    uint8_t isr_ch = (isr >> (4 * ch)) & 0x0f;
    uint8_t ifcr_ch = 0;
    switch (ch) {
    case DmaChUart1Rx:
        ifcr_ch = uart_dma_irq(isr_ch);
        break;
    default:
        DBG_BKPT("Unknown DMA channel");
    }
    return ((uint32_t)ifcr_ch) << (4 * ch);
}

void DMA1_Channel1_IRQHandler()
{
    uint32_t isr = DMA1->ISR;
    uint32_t ifcr = 0;
    ifcr |= dma_irq(isr, 0);
    DMA1->IFCR = ifcr;
}
