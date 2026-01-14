#include <stdbool.h>
#include "stm32f7xx.h"
#include "systick.h"
#include "semihosting.h"
#include "bootloader.h"
#include "macros.h"
#include "irq.h"
#include "usb.h"
#include "usb_dfu.h"

extern void Reset_Handler();

// Header to tell flash bootloader the entry point
static bootloader_req_t req USED SECTION(.fw_header) = {
    .op = BootloaderRunItcm,
    .ptr = &Reset_Handler,
};

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
    RCC->APB1ENR = RCC_APB1ENR_PWREN;
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
    // Set dedicated clocks: CLK48 from PLL
    RCC->DCKCFGR2 = 0;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL);
    // Enable clock output for USB3370 PHY
    // MCO1: HSE / 1
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE)) |
        (0b10 << RCC_CFGR_MCO1_Pos) | (0 << RCC_CFGR_MCO1PRE_Pos);
}

static void board_init()
{
    SCB_EnableICache();
    SCB_EnableDCache();
    rcc_init();
    panic_init();

    // Configure interrupt vector table location
    SCB->VTOR = (uint32_t)irq_vectors;
    // Enable all interrupts
    NVIC_SetPriorityGrouping(NVIC_PRIORITY_GROUPING);
    __enable_irq();

    systick_init();

    // Enable peripherals
    RCC->AHB1ENR = RCC_AHB1ENR_OTGHSULPIEN_Msk | RCC_AHB1ENR_OTGHSEN_Msk | RCC_AHB1ENR_DTCMRAMEN_Msk |
        RCC_AHB1ENR_GPIOAEN_Msk | RCC_AHB1ENR_GPIOBEN_Msk | RCC_AHB1ENR_GPIOCEN_Msk;
    RCC->AHB2ENR = RCC_AHB2ENR_OTGFSEN_Msk;
    RCC->APB2ENR = RCC_APB2ENR_SYSCFGEN_Msk;

    // Configure GPIOs
    // PA3  | USB_OTG_HS_ULPI_D0  | AF10 | HS 60M
    // PA5  | USB_OTG_HS_ULPI_CK  | AF10 | HS 60M
    // PA8  | RCC_MCO_1           | AF0  | MS 19.2M
    // PA11 | USB_OTG_FS_DM       | AF10 | MS 12M
    // PA12 | USB_OTG_FS_DP       | AF10 | MS 12M
    // PA13 | SYS_JTMS-SWDIO      | AF0  | HS ?
    // PA14 | SYS_JTCK-SWCLK      | AF0  | HS ?
    // PB0  | USB_OTG_HS_ULPI_D1  | AF10 | HS 60M
    // PB1  | USB_OTG_HS_ULPI_D2  | AF10 | HS 60M
    // PB5  | USB_OTG_HS_ULPI_D7  | AF10 | HS 60M
    // PB6  | I2C1_SCL            | AF4  | LS 400k
    // PB7  | I2C1_SDA            | AF4  | LS 400k
    // PB10 | USB_OTG_HS_ULPI_D3  | AF10 | HS 60M
    // PB11 | USB_OTG_HS_ULPI_D4  | AF10 | HS 60M
    // PB12 | USB_OTG_HS_ULPI_D5  | AF10 | HS 60M
    // PB13 | USB_OTG_HS_ULPI_D6  | AF10 | HS 60M
    // PC0  | USB_OTG_HS_ULPI_STP | AF10 | HS 60M
    // PC2  | USB_OTG_HS_ULPI_DIR | AF10 | HS 60M
    // PC3  | USB_OTG_HS_ULPI_NXT | AF10 | HS 60M
    // PH0  | RCC_OSC_IN          | AF10 | 19.2M
    // PH1  | RCC_OSC_OUT         | AF10 | 19.2M

    // Enable IO compensation cell
    SYSCFG->CMPCR = SYSCFG_CMPCR_CMP_PD_Msk;
    // For unusued pins, disable input Schmitt trigger for power saving
    // 10: Alternative function mode
    // 11: Analog mode
    GPIOA->MODER =
        (0b11ul << GPIO_MODER_MODER0_Pos)  | (0b11ul << GPIO_MODER_MODER1_Pos)  |
        (0b11ul << GPIO_MODER_MODER2_Pos)  | (0b10ul << GPIO_MODER_MODER3_Pos)  |
        (0b11ul << GPIO_MODER_MODER4_Pos)  | (0b10ul << GPIO_MODER_MODER5_Pos)  |
        (0b11ul << GPIO_MODER_MODER6_Pos)  | (0b11ul << GPIO_MODER_MODER7_Pos)  |
        (0b10ul << GPIO_MODER_MODER8_Pos)  | (0b11ul << GPIO_MODER_MODER9_Pos)  |
        (0b11ul << GPIO_MODER_MODER10_Pos) | (0b10ul << GPIO_MODER_MODER11_Pos) |
        (0b10ul << GPIO_MODER_MODER12_Pos) | (0b10ul << GPIO_MODER_MODER13_Pos) |
        (0b10ul << GPIO_MODER_MODER14_Pos) | (0b11ul << GPIO_MODER_MODER15_Pos);
    GPIOB->MODER =
        (0b10ul << GPIO_MODER_MODER0_Pos)  | (0b10ul << GPIO_MODER_MODER1_Pos)  |
        (0b11ul << GPIO_MODER_MODER2_Pos)  | (0b11ul << GPIO_MODER_MODER3_Pos)  |
        (0b11ul << GPIO_MODER_MODER4_Pos)  | (0b10ul << GPIO_MODER_MODER5_Pos)  |
        (0b10ul << GPIO_MODER_MODER6_Pos)  | (0b10ul << GPIO_MODER_MODER7_Pos)  |
        (0b11ul << GPIO_MODER_MODER8_Pos)  | (0b11ul << GPIO_MODER_MODER9_Pos)  |
        (0b10ul << GPIO_MODER_MODER10_Pos) | (0b10ul << GPIO_MODER_MODER11_Pos) |
        (0b10ul << GPIO_MODER_MODER12_Pos) | (0b10ul << GPIO_MODER_MODER13_Pos) |
        (0b11ul << GPIO_MODER_MODER14_Pos) | (0b11ul << GPIO_MODER_MODER15_Pos);
    GPIOC->MODER =
        (0b10ul << GPIO_MODER_MODER0_Pos)  | (0b11ul << GPIO_MODER_MODER1_Pos)  |
        (0b10ul << GPIO_MODER_MODER2_Pos)  | (0b10ul << GPIO_MODER_MODER3_Pos)  |
        (0b11ul << GPIO_MODER_MODER4_Pos)  | (0b11ul << GPIO_MODER_MODER5_Pos)  |
        (0b11ul << GPIO_MODER_MODER6_Pos)  | (0b11ul << GPIO_MODER_MODER7_Pos)  |
        (0b11ul << GPIO_MODER_MODER8_Pos)  | (0b11ul << GPIO_MODER_MODER9_Pos)  |
        (0b11ul << GPIO_MODER_MODER10_Pos) | (0b11ul << GPIO_MODER_MODER11_Pos) |
        (0b11ul << GPIO_MODER_MODER12_Pos) | (0b11ul << GPIO_MODER_MODER13_Pos) |
        (0b11ul << GPIO_MODER_MODER14_Pos) | (0b11ul << GPIO_MODER_MODER15_Pos);
    GPIOA->OTYPER = 0;
    GPIOB->OTYPER = GPIO_OTYPER_OT6_Msk | GPIO_OTYPER_OT7_Msk;
    GPIOC->OTYPER = 0;
    GPIOA->OSPEEDR =
        (0b10ul << GPIO_OSPEEDR_OSPEEDR3_Pos) | (0b10ul << GPIO_OSPEEDR_OSPEEDR5_Pos) |
        (0b01ul << GPIO_OSPEEDR_OSPEEDR8_Pos) | (0b01ul << GPIO_OSPEEDR_OSPEEDR11_Pos) |
        (0b01ul << GPIO_OSPEEDR_OSPEEDR12_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR13_Pos) | (0b10ul << GPIO_OSPEEDR_OSPEEDR14_Pos);
    GPIOB->OSPEEDR =
        (0b10ul << GPIO_OSPEEDR_OSPEEDR0_Pos) | (0b10ul << GPIO_OSPEEDR_OSPEEDR1_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR5_Pos) |
        (0b00ul << GPIO_OSPEEDR_OSPEEDR6_Pos) | (0b00ul << GPIO_OSPEEDR_OSPEEDR7_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR10_Pos) | (0b10ul << GPIO_OSPEEDR_OSPEEDR11_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR12_Pos) | (0b10ul << GPIO_OSPEEDR_OSPEEDR13_Pos);
    GPIOC->OSPEEDR =
        (0b10ul << GPIO_OSPEEDR_OSPEEDR0_Pos) | (0b10ul << GPIO_OSPEEDR_OSPEEDR2_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR3_Pos);
    GPIOA->PUPDR = (0b01ul << GPIO_PUPDR_PUPDR13_Pos) | (0b10ul << GPIO_PUPDR_PUPDR14_Pos);
    GPIOB->PUPDR = 0;
    GPIOC->PUPDR = 0;
#define GPIO_AFRH_AFRH8_Pos  GPIO_AFRH_AFRH0_Pos
#define GPIO_AFRH_AFRH9_Pos  GPIO_AFRH_AFRH1_Pos
#define GPIO_AFRH_AFRH10_Pos GPIO_AFRH_AFRH2_Pos
#define GPIO_AFRH_AFRH11_Pos GPIO_AFRH_AFRH3_Pos
#define GPIO_AFRH_AFRH12_Pos GPIO_AFRH_AFRH4_Pos
#define GPIO_AFRH_AFRH13_Pos GPIO_AFRH_AFRH5_Pos
#define GPIO_AFRH_AFRH14_Pos GPIO_AFRH_AFRH6_Pos
#define GPIO_AFRH_AFRH15_Pos GPIO_AFRH_AFRH7_Pos
    GPIOA->AFR[0] =
        (10ul << GPIO_AFRL_AFRL3_Pos) | (10ul << GPIO_AFRL_AFRL5_Pos);
    GPIOA->AFR[1] =
        (0ul << GPIO_AFRH_AFRH8_Pos) |
        (10ul << GPIO_AFRH_AFRH11_Pos) | (10ul << GPIO_AFRH_AFRH12_Pos) |
        (0ul << GPIO_AFRH_AFRH13_Pos) | (0ul << GPIO_AFRH_AFRH14_Pos);
    GPIOB->AFR[0] =
        (10ul << GPIO_AFRL_AFRL0_Pos) | (10ul << GPIO_AFRL_AFRL1_Pos) |
        (10ul << GPIO_AFRL_AFRL5_Pos) |
        (4ul << GPIO_AFRL_AFRL6_Pos) | (4ul << GPIO_AFRL_AFRL7_Pos);
    GPIOB->AFR[1] =
        (10ul << GPIO_AFRH_AFRH10_Pos) | (10ul << GPIO_AFRH_AFRH11_Pos) |
        (10ul << GPIO_AFRH_AFRH12_Pos) | (10ul << GPIO_AFRH_AFRH13_Pos);
    GPIOC->AFR[0] =
        (10ul << GPIO_AFRL_AFRL0_Pos) | (10ul << GPIO_AFRL_AFRL2_Pos) |
        (10ul << GPIO_AFRL_AFRL3_Pos);
    GPIOC->AFR[1] = 0;
    // Wait for IO compensation cell
    while (!(SYSCFG->CMPCR & SYSCFG_CMPCR_READY_Msk));

    usb_init(UsbIfFs);
    usb_init(UsbIfHs);
}

void main()
{
    board_init();

    // dbg_puts("bootloader\r\n");
    // dbg_bkpt();

    // dbg_puts("USB connect\r\n");
    usb_connect(UsbIfFs, true);
    usb_connect(UsbIfHs, true);
    // dbg_bkpt();

    // dbg_puts("USB disconnect\r\n");
    // usb_connect(UsbIfFs, false);
    // usb_connect(UsbIfHs, false);
    // dbg_bkpt();

    // uint32_t ms = systick_ms();
    // while (systick_ms() - ms < 5000);
    // dbg_bkpt();

    for (;;) {
        usb_process(UsbIfFs);
        usb_process(UsbIfHs);
        usb_dfu_process();
    }

//     for (;;) {
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
