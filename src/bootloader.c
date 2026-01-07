#include <stdbool.h>
#include "stm32f7xx.h"
#include "systick.h"
// #include "dma.h"
// #include "usb.h"
#include "semihosting.h"
#include "irq.h"

static void mco1_init()
{
	// MCO1: HSE / 1
	RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE)) |
			(0b10 << RCC_CFGR_MCO1_Pos) | (0 << RCC_CFGR_MCO1PRE_Pos);
	// Enable IO compensation cell
	if (!(SYSCFG->CMPCR & SYSCFG_CMPCR_READY)) {
		RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
		SYSCFG->CMPCR |= SYSCFG_CMPCR_CMP_PD;
	}
	// Configure GPIOs
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    // 10: Alternative function mode
    GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER8_Msk) | (0b10 << GPIO_MODER_MODER8_Pos);
	// Output push-pull
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT8_Msk;
	// High speed (50~100MHz)
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~GPIO_OSPEEDR_OSPEEDR8_Msk) | (0b10 << GPIO_OSPEEDR_OSPEEDR8_Pos);
	// AF0: MCO1
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~GPIO_AFRH_AFRH0_Msk) | (0 << GPIO_AFRH_AFRH0_Pos);
	// Wait for IO compensation cell
	while (!(SYSCFG->CMPCR & SYSCFG_CMPCR_READY));
}

static void rcc_init()
{
	// Enable HSE
	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY));
	// Switch to HSE
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_HSE;
	while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_HSE);
	// Disable HSI and PLL
	RCC->CR &= ~(RCC_CR_HSION | RCC_CR_PLLON);
	while (RCC->CR & RCC_CR_PLLRDY);
	// Configure PLL (HSE, PLLM = 12, PLLN = 270, PLLP = 2, PLLQ = 9)
	RCC->PLLCFGR = (12u << RCC_PLLCFGR_PLLM_Pos) | (270u << RCC_PLLCFGR_PLLN_Pos) |
			(0u << RCC_PLLCFGR_PLLP_Pos) | (9u << RCC_PLLCFGR_PLLQ_Pos) |
			RCC_PLLCFGR_PLLSRC_HSE;
	// Enable power controller
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	// Regulator voltage scale 1
	PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | (0b11 << PWR_CR1_VOS_Pos);
	// Enable PLL
	RCC->CR |= RCC_CR_PLLON;
	// Enable Over-drive
	PWR->CR1 |= PWR_CR1_ODEN;
	while (!(PWR->CSR1 & PWR_CSR1_ODRDY));
	PWR->CR1 |= PWR_CR1_ODSWEN;
	while (!(PWR->CSR1 & PWR_CSR1_ODSWRDY));
	// Set flash latency
	// ART enable, prefetch enable, 7 wait states
	FLASH->ACR = FLASH_ACR_ARTEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_7WS;
	// Set AHB & APB prescalers
	// AHB = 1, APB1 = 4, APB2 = 2
	RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
			RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
	// Wait for PLL lock
	while (!(RCC->CR & RCC_CR_PLLRDY));
	// Switch to PLL
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
	// Set dedicated clocks
	RCC->DCKCFGR2 = 0;
	while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL);
	// Update clock configuration
	// SystemCoreClockUpdate();
	// Enable clock output for other chips
	mco1_init();
}

static void board_init()
{
	SCB_EnableICache();
	SCB_EnableDCache();
	rcc_init();

    // Configure interrupt vector table location
    extern uint32_t __isr_vector_start;
    SCB->VTOR = (uint32_t)&__isr_vector_start;
    // Enable all interrupts
	NVIC_SetPriorityGrouping(NVIC_PRIORITY_GROUPING);
    __enable_irq();

	systick_init();
}

// void usb_reset_handler()
// {
//     bt_hci_h4_reset();
// }

void main()
{
    board_init();

    dbg_puts("bootloader\r\n");
    dbg_bkpt();

    for (;;) {}

//     uint32_t last_ms = systick_ms();
//     bool led = false;
//     led_set(0, led);

//     uint32_t debouncing_ms = 0;
//     bool debouncing = false;
//     bool btn = false;
//     bool usb = true;

//     led_set(1, usb);
//     usb_connect(usb);

//     for (;;) {
//         uint32_t now_ms = systick_ms();
//         if (now_ms - last_ms >= 1000) {
//             last_ms += 1000;
//             led = !led;
//             led_set(0, led);
//         }

//         usb_process();
//         // usb_hid_process(now_ms);

// #if 0
//         if (uart_rx_available() && usb_cdc_tx_free())
//             usb_cdc_tx_write(uart_rx());

//         if (usb_cdc_rx_available() && uart_tx_free())
//             uart_tx(usb_cdc_rx_read());
// #endif

//     }
}
