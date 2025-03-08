#include "stm32c071xx.h"
#include "semihosting.h"

#define RCC_CFGR_SW_HSI48   (0b010 << RCC_CFGR_SW_Pos)

void board_init()
{
    // Enable HSIUSB48 clock
    RCC->CR |= RCC_CR_HSIUSB48ON;
    // Configure flash latency, enable instruction cache and prefetch
    FLASH->ACR = FLASH_ACR_DBG_SWEN | FLASH_ACR_ICEN | FLASH_ACR_PRFTEN |
        (1 << FLASH_ACR_LATENCY_Pos);
    // Switch to HSIUSB48 clock
    while (!(RCC->CR & RCC_CR_HSIUSB48RDY));
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_HSI48;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_HSI48);
    // Configure clock dividers and disable other clocks
    RCC->CR = RCC_CR_HSIUSB48ON;
    RCC->CFGR = RCC_CFGR_SW_HSI48;

    // Enable peripherals
    RCC->AHBENR = RCC_AHBENR_FLASHEN | RCC_AHBENR_DMA1EN;
    RCC->APBENR1 = RCC_APBENR1_DBGEN | RCC_APBENR1_USART2EN | RCC_APBENR1_USBEN;
    RCC->IOPENR = RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOCEN;
}

void main()
{
    board_init();

    dbg_puts("Hello, world!\r\n");
    for (;;)
        dbg_bkpt();
}
