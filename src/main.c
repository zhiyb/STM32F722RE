#include <stdbool.h>
#include "stm32c0xx.h"
#include "systick.h"
#include "uart.c"
#include "usb.h"
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
    RCC->APBENR1 = RCC_APBENR1_DBGEN | RCC_APBENR1_USART2EN | RCC_APBENR1_USBEN | RCC_APBENR1_CRSEN;
    RCC->APBENR2 = RCC_APBENR2_USART1EN;
    RCC->IOPENR = RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;

    // Configure GPIOs
    // PA2:  USART2_TX, AF1
    // PA3:  USART2_RX, AF1, pull-up
    // PA5:  LED1, output, active-high
    // PA8:  USART1_RX, AF14, pull-up
    // PA9:  USART1_TX, AF1
    // PA11: USB_DM, analog
    // PA12: USB_DP, analog
    // PA13: SWDIO, AF0
    // PA14: SWCLK, AF0
    GPIOA->AFR[0] = (1 << GPIO_AFRL_AFSEL2_Pos) | (1 << GPIO_AFRL_AFSEL3_Pos);
    GPIOA->AFR[1] = (14 << GPIO_AFRH_AFSEL8_Pos) | (1 << GPIO_AFRH_AFSEL9_Pos) |
        (0 << GPIO_AFRH_AFSEL13_Pos) | (0 << GPIO_AFRH_AFSEL14_Pos);
    GPIOA->PUPDR = (0b01 << GPIO_PUPDR_PUPD3_Pos) | (0b01 << GPIO_PUPDR_PUPD8_Pos);
    GPIOA->ODR = 0;
    GPIOA->MODER = (0b10 << GPIO_MODER_MODE2_Pos) | (0b10 << GPIO_MODER_MODE3_Pos) |
        (0b01 << GPIO_MODER_MODE5_Pos) |
        (0b10 << GPIO_MODER_MODE8_Pos) | (0b10 << GPIO_MODER_MODE9_Pos) |
        (0b11 << GPIO_MODER_MODE11_Pos) | (0b11 << GPIO_MODER_MODE12_Pos) |
        (0b10 << GPIO_MODER_MODE13_Pos) | (0b10 << GPIO_MODER_MODE14_Pos);

    // PB3:  USART1_RTS, AF4
    // PB4:  USART1_CTS, AF4, pull-up
    GPIOB->AFR[0] = (4 << GPIO_AFRL_AFSEL3_Pos) | (4 << GPIO_AFRL_AFSEL4_Pos);
    GPIOB->PUPDR = (0b01 << GPIO_PUPDR_PUPD4_Pos);
    GPIOB->MODER = (0b10 << GPIO_MODER_MODE3_Pos) | (0b10 << GPIO_MODER_MODE4_Pos);

    // PC9:  LED2, output, active-low
    // PC13: BTN, input, active-low
    GPIOC->PUPDR = (0b01 << GPIO_PUPDR_PUPD13_Pos);
    GPIOC->ODR = GPIO_ODR_OD9_Msk;
    GPIOC->MODER = (0b01 << GPIO_MODER_MODE9_Pos) | (0b00 << GPIO_MODER_MODE13_Pos);

    systick_init();
    uart_init();
    usb_init();

    // Configure NVIC interrupt priorities
    NVIC_SetPriority(USB_DRD_FS_IRQn, 4);
    NVIC_SetPriority(SysTick_IRQn, 8);
    // Configure interrupt vector table location
    extern uint32_t __isr_vector_start;
    SCB->VTOR = (uint32_t)&__isr_vector_start;
    // Enable all interrupts
    __enable_irq();
}

void led_set(int led, bool state)
{
    if (led == 0) {
        // PA5, active-high
        if (state)
            GPIOA->BSRR = GPIO_BSRR_BS5_Msk;
        else
            GPIOA->BSRR = GPIO_BSRR_BR5_Msk;
    } else {
        // PC9, active-low
        if (state)
            GPIOC->BSRR = GPIO_BSRR_BR9_Msk;
        else
            GPIOC->BSRR = GPIO_BSRR_BS9_Msk;
    }
}

int button_read()
{
    return !(GPIOC->IDR & GPIO_IDR_ID13_Msk);
}

void main()
{
    board_init();

    // dbg_puts("Hello, world!\r\n");
    // dbg_bkpt();

    uint32_t last_ms = systick_ms();
    bool led = false;
    led_set(0, led);

    uint32_t debouncing_ms = 0;
    bool debouncing = false;
    bool btn = false;
    bool usb = true;

    led_set(1, usb);
    usb_connect(usb);

    for (;;) {
        uint32_t now_ms = systick_ms();
        if (now_ms - last_ms >= 1000) {
            last_ms += 1000;
            led = !led;
            led_set(0, led);
        }

        if (debouncing) {
            if (now_ms - debouncing_ms >= 100)
                debouncing = false;
        } else {
            bool btn_now = button_read();
            if (btn != btn_now) {
                btn = btn_now;
                debouncing = true;
                if (btn_now) {
                    usb = !usb;
                    led_set(1, usb);
                    usb_connect(usb);
                }
            }
        }

        usb_process();
        usb_hid_process(now_ms);

        if (uart_rx_available() && usb_cdc_tx_free())
            usb_cdc_tx_write(uart_rx());

        if (usb_cdc_rx_available() && uart_tx_free())
            uart_tx(usb_cdc_rx_read());

#if 0
        uint16_t cdc_rx_avail = usb_cdc_rx_available();
        uint16_t cdc_tx_free = usb_cdc_tx_free();
        uint16_t avail = cdc_rx_avail < cdc_tx_free ? cdc_rx_avail : cdc_tx_free;
        for (uint16_t i = 0; i < avail; i++) {
            uint8_t v = usb_cdc_rx_read();

            // Mouse control test
            int8_t x = 0, y = 0, d = 8;
            switch (v) {
            case 'w':
                y = -d;
                break;
            case 's':
                y = d;
                break;
            case 'a':
                x = -d;
                break;
            case 'd':
                x = d;
                break;
            }
            if (x != 0 || y != 0)
                usb_hid_mouse_move(x, y);

            // Serial port echo test
            if (v >= 'a' && v < 'z')
                v++;    // Test
            usb_cdc_tx_write(v);

            if (uart_tx_free())
                uart_tx(v);
        }

        if (uart_rx_available())
            usb_cdc_tx_write(uart_rx());
#endif
    }
}
