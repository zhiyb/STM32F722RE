#include <stdbool.h>
#include "stm32f7xx.h"
#include "systick.h"
#include "semihosting.h"
#include "bootloader.h"
#include "macros.h"
#include "irq.h"
#include "flash.h"
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
#ifdef ENABLE_USB_HS_MODE_ULPI
    // Enable clock output for USB3370 PHY
    // MCO1: HSE / 1
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE)) |
        (0b10 << RCC_CFGR_MCO1_Pos) | (0 << RCC_CFGR_MCO1PRE_Pos);
#endif
}

static void board_init()
{
    rcc_init();
    panic_init();

    // Initialise MPU and cache
    static const ARM_MPU_Region_t mpu_regions[] = {
        {
            // Disable exec, read cache and write cache on flash AXIM port
            // See errata: Cortex-M7 data corruption when using data cache configured in write-through
            // Also to make flash read/write easier, no cache maintenance needed
            .RBAR = ARM_MPU_RBAR(0, FLASHAXI_BASE),
            .RASR = ARM_MPU_RASR(1, ARM_MPU_AP_PRIV, 0b000, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_512KB)
        }, {
            // Also for flash information block
            .RBAR = ARM_MPU_RBAR(0, 0x1ff00000),
            .RASR = ARM_MPU_RASR(1, ARM_MPU_AP_PRIV, 0b000, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_1MB)
        },
    };
    ARM_MPU_Load(mpu_regions, ARRAY_SIZE(mpu_regions));
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);		// Fallback to default mapping
    SCB_EnableICache();
    SCB_EnableDCache();

    // Configure interrupt vector table location
    SCB->VTOR = (uint32_t)irq_vectors;
    // Enable all interrupts
    NVIC_SetPriorityGrouping(NVIC_PRIORITY_GROUPING);
    __enable_irq();

    systick_init();

    // Enable peripherals
    RCC->AHB1ENR =
#ifdef ENABLE_USB_HS_MODE_ULPI
        RCC_AHB1ENR_OTGHSULPIEN_Msk |
#endif
#ifdef ENABLE_USB_HS
        RCC_AHB1ENR_OTGHSEN_Msk |
#endif
        RCC_AHB1ENR_DTCMRAMEN_Msk |
        RCC_AHB1ENR_GPIOAEN_Msk | RCC_AHB1ENR_GPIOBEN_Msk | RCC_AHB1ENR_GPIOCEN_Msk;
    RCC->AHB2ENR = RCC_AHB2ENR_OTGFSEN_Msk;
    RCC->APB2ENR = RCC_APB2ENR_SYSCFGEN_Msk;

    // Configure GPIOs
#ifdef ENABLE_USB_HS_MODE_ULPI
    // PA3  | USB_OTG_HS_ULPI_D0  | AF10 | HS 60M
    // PA5  | USB_OTG_HS_ULPI_CK  | AF10 | HS 60M
    // PA8  | RCC_MCO_1           | AF0  | MS 19.2M
#endif
#ifdef ENABLE_USB_FS
    // PA11 | USB_OTG_FS_DM       | AF10 | MS 12M
    // PA12 | USB_OTG_FS_DP       | AF10 | MS 12M
#endif
    // PA13 | SYS_JTMS-SWDIO      | AF0  | HS ?
    // PA14 | SYS_JTCK-SWCLK      | AF0  | HS ?
#ifdef ENABLE_USB_HS_MODE_ULPI
    // PB0  | USB_OTG_HS_ULPI_D1  | AF10 | HS 60M
    // PB1  | USB_OTG_HS_ULPI_D2  | AF10 | HS 60M
    // PB5  | USB_OTG_HS_ULPI_D7  | AF10 | HS 60M
#endif
#ifdef ENABLE_USB_HS_MODE_ULPI
    // PB10 | USB_OTG_HS_ULPI_D3  | AF10 | HS 60M
    // PB11 | USB_OTG_HS_ULPI_D4  | AF10 | HS 60M
    // PB12 | USB_OTG_HS_ULPI_D5  | AF10 | HS 60M
    // PB13 | USB_OTG_HS_ULPI_D6  | AF10 | HS 60M
#endif
#ifdef ENABLE_USB_HS_MODE_FS
    // PB14 | USB_OTG_HS_DM       | AF12 | MS 12M
    // PB15 | USB_OTG_HS_DP       | AF12 | MS 12M
#endif
#ifdef ENABLE_USB_HS_MODE_ULPI
    // PC0  | USB_OTG_HS_ULPI_STP | AF10 | HS 60M
    // PC2  | USB_OTG_HS_ULPI_DIR | AF10 | HS 60M
    // PC3  | USB_OTG_HS_ULPI_NXT | AF10 | HS 60M
#endif
    // PH0  | RCC_OSC_IN          | AF10 | 19.2M
    // PH1  | RCC_OSC_OUT         | AF10 | 19.2M

#ifdef ENABLE_USB_HS_MODE_ULPI
    // Enable IO compensation cell
    SYSCFG->CMPCR = SYSCFG_CMPCR_CMP_PD_Msk;
#endif
    // For unusued pins, disable input Schmitt trigger for power saving
    // 10: Alternative function mode
    // 11: Analog mode
    GPIOA->MODER =
#ifdef ENABLE_USB_FS
        (0b10ul << GPIO_MODER_MODER11_Pos) |
        (0b10ul << GPIO_MODER_MODER12_Pos) |
#else
        (0b11ul << GPIO_MODER_MODER11_Pos) |
        (0b11ul << GPIO_MODER_MODER12_Pos) |
#endif
#ifdef ENABLE_USB_HS_MODE_ULPI
        (0b10ul << GPIO_MODER_MODER3_Pos)  |
        (0b10ul << GPIO_MODER_MODER5_Pos)  |
        (0b10ul << GPIO_MODER_MODER8_Pos)  |
#else
        (0b11ul << GPIO_MODER_MODER3_Pos)  |
        (0b11ul << GPIO_MODER_MODER5_Pos)  |
        (0b11ul << GPIO_MODER_MODER8_Pos)  |
#endif
        (0b11ul << GPIO_MODER_MODER0_Pos)  |
        (0b11ul << GPIO_MODER_MODER1_Pos)  |
        (0b11ul << GPIO_MODER_MODER2_Pos)  |
        (0b11ul << GPIO_MODER_MODER4_Pos)  |
        (0b11ul << GPIO_MODER_MODER6_Pos)  |
        (0b11ul << GPIO_MODER_MODER7_Pos)  |
        (0b11ul << GPIO_MODER_MODER9_Pos)  |
        (0b11ul << GPIO_MODER_MODER10_Pos) |
        (0b10ul << GPIO_MODER_MODER13_Pos) |
        (0b10ul << GPIO_MODER_MODER14_Pos) |
        (0b11ul << GPIO_MODER_MODER15_Pos);
    GPIOB->MODER =
#ifdef ENABLE_USB_HS_MODE_FS
        (0b10ul << GPIO_MODER_MODER14_Pos) |
        (0b10ul << GPIO_MODER_MODER15_Pos) |
#else
        (0b11ul << GPIO_MODER_MODER14_Pos) |
        (0b11ul << GPIO_MODER_MODER15_Pos) |
#endif
#ifdef ENABLE_USB_HS_MODE_ULPI
        (0b10ul << GPIO_MODER_MODER0_Pos)  |
        (0b10ul << GPIO_MODER_MODER1_Pos)  |
        (0b10ul << GPIO_MODER_MODER5_Pos)  |
        (0b10ul << GPIO_MODER_MODER10_Pos) |
        (0b10ul << GPIO_MODER_MODER11_Pos) |
        (0b10ul << GPIO_MODER_MODER12_Pos) |
        (0b10ul << GPIO_MODER_MODER13_Pos) |
#else
        (0b11ul << GPIO_MODER_MODER0_Pos)  |
        (0b11ul << GPIO_MODER_MODER1_Pos)  |
        (0b11ul << GPIO_MODER_MODER5_Pos)  |
        (0b11ul << GPIO_MODER_MODER10_Pos) |
        (0b11ul << GPIO_MODER_MODER11_Pos) |
        (0b11ul << GPIO_MODER_MODER12_Pos) |
        (0b11ul << GPIO_MODER_MODER13_Pos) |
#endif
        (0b11ul << GPIO_MODER_MODER2_Pos)  |
        (0b11ul << GPIO_MODER_MODER3_Pos)  |
        (0b11ul << GPIO_MODER_MODER4_Pos)  |
        (0b11ul << GPIO_MODER_MODER6_Pos)  |
        (0b11ul << GPIO_MODER_MODER7_Pos)  |
        (0b11ul << GPIO_MODER_MODER8_Pos)  |
        (0b11ul << GPIO_MODER_MODER9_Pos);
    GPIOC->MODER =
#ifdef ENABLE_USB_HS_MODE_ULPI
        (0b10ul << GPIO_MODER_MODER0_Pos)  |
        (0b10ul << GPIO_MODER_MODER2_Pos)  |
        (0b10ul << GPIO_MODER_MODER3_Pos)  |
#else
        (0b11ul << GPIO_MODER_MODER0_Pos)  |
        (0b11ul << GPIO_MODER_MODER2_Pos)  |
        (0b11ul << GPIO_MODER_MODER3_Pos)  |
#endif
        (0b11ul << GPIO_MODER_MODER1_Pos)  |
        (0b11ul << GPIO_MODER_MODER4_Pos)  |
        (0b11ul << GPIO_MODER_MODER5_Pos)  |
        (0b11ul << GPIO_MODER_MODER6_Pos)  |
        (0b11ul << GPIO_MODER_MODER7_Pos)  |
        (0b11ul << GPIO_MODER_MODER8_Pos)  |
        (0b11ul << GPIO_MODER_MODER9_Pos)  |
        (0b11ul << GPIO_MODER_MODER10_Pos) |
        (0b11ul << GPIO_MODER_MODER11_Pos) |
        (0b11ul << GPIO_MODER_MODER12_Pos) |
        (0b11ul << GPIO_MODER_MODER13_Pos) |
        (0b11ul << GPIO_MODER_MODER14_Pos) |
        (0b11ul << GPIO_MODER_MODER15_Pos);
    GPIOA->OTYPER = 0;
    GPIOB->OTYPER = 0;
    GPIOC->OTYPER = 0;
    GPIOA->OSPEEDR =
#ifdef ENABLE_USB_FS
        (0b01ul << GPIO_OSPEEDR_OSPEEDR11_Pos) |
        (0b01ul << GPIO_OSPEEDR_OSPEEDR12_Pos) |
#endif
#ifdef ENABLE_USB_HS_MODE_ULPI
        (0b10ul << GPIO_OSPEEDR_OSPEEDR3_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR5_Pos) |
        (0b01ul << GPIO_OSPEEDR_OSPEEDR8_Pos) |
#endif
        (0b10ul << GPIO_OSPEEDR_OSPEEDR13_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR14_Pos);
    GPIOB->OSPEEDR =
#ifdef ENABLE_USB_HS_MODE_FS
        (0b01ul << GPIO_OSPEEDR_OSPEEDR14_Pos) |
        (0b01ul << GPIO_OSPEEDR_OSPEEDR15_Pos) |
#endif
#ifdef ENABLE_USB_HS_MODE_ULPI
        (0b10ul << GPIO_OSPEEDR_OSPEEDR0_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR1_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR5_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR10_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR11_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR12_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR13_Pos) |
#endif
        0;
    GPIOC->OSPEEDR =
#ifdef ENABLE_USB_HS_MODE_ULPI
        (0b10ul << GPIO_OSPEEDR_OSPEEDR0_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR2_Pos) |
        (0b10ul << GPIO_OSPEEDR_OSPEEDR3_Pos) |
#endif
        0;
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
        (10ul << GPIO_AFRL_AFRL5_Pos);
    GPIOB->AFR[1] =
        (10ul << GPIO_AFRH_AFRH10_Pos) | (10ul << GPIO_AFRH_AFRH11_Pos) |
        (10ul << GPIO_AFRH_AFRH12_Pos) | (10ul << GPIO_AFRH_AFRH13_Pos) |
        (12ul << GPIO_AFRH_AFRH14_Pos) | (12ul << GPIO_AFRH_AFRH15_Pos);
    GPIOC->AFR[0] =
        (10ul << GPIO_AFRL_AFRL0_Pos) | (10ul << GPIO_AFRL_AFRL2_Pos) |
        (10ul << GPIO_AFRL_AFRL3_Pos);
    GPIOC->AFR[1] = 0;
#ifdef ENABLE_USB_HS_MODE_ULPI
    // Wait for IO compensation cell
    while (!(SYSCFG->CMPCR & SYSCFG_CMPCR_READY_Msk));
#endif

    flash_init();

#ifdef ENABLE_USB_FS
    usb_init(UsbIfFs);
#endif
#ifdef ENABLE_USB_HS
    usb_init(UsbIfHs);
#endif
}

void main()
{
    board_init();

#ifdef ENABLE_USB_FS
    usb_connect(UsbIfFs, true);
#endif
#ifdef ENABLE_USB_HS
    usb_connect(UsbIfHs, true);
#endif

    for (;;) {
#ifdef ENABLE_USB_FS
        usb_process(UsbIfFs);
#endif
#ifdef ENABLE_USB_HS
        usb_process(UsbIfHs);
#endif
    }
}
