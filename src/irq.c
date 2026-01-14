#include "macros.h"
#include "semihosting.h"

#define NAKED           __attribute__((naked))
#define WEAK            __attribute__((weak))
#define ALIAS_DEFAULT   __attribute__((alias("Default_Handler")))

extern char __stack_end;

WEAK void Default_Handler()
{
#ifdef BOOTLOADER_FLASH
    for (;;)
        dbg_bkpt();
#else
    PANIC("Unhandled interrupt");
#endif
}

WEAK void Reset_Handler() ALIAS_DEFAULT;

WEAK void NMI_Handler() ALIAS_DEFAULT;
WEAK void HardFault_Handler() ALIAS_DEFAULT;
WEAK void MemManage_Handler() ALIAS_DEFAULT;
WEAK void BusFault_Handler() ALIAS_DEFAULT;
WEAK void UsageFault_Handler() ALIAS_DEFAULT;
WEAK void SVC_Handler() ALIAS_DEFAULT;
WEAK void DebugMon_Handler() ALIAS_DEFAULT;
WEAK void PendSV_Handler() ALIAS_DEFAULT;
WEAK void SysTick_Handler() ALIAS_DEFAULT;

WEAK void WWDG_IRQHandler() ALIAS_DEFAULT;
WEAK void PVD_IRQHandler() ALIAS_DEFAULT;
WEAK void TAMP_STAMP_IRQHandler() ALIAS_DEFAULT;
WEAK void RTC_WKUP_IRQHandler() ALIAS_DEFAULT;
WEAK void FLASH_IRQHandler() ALIAS_DEFAULT;
WEAK void RCC_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI0_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI1_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI2_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI3_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI4_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream0_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream1_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream2_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream3_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream4_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream5_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream6_IRQHandler() ALIAS_DEFAULT;
WEAK void ADC_IRQHandler() ALIAS_DEFAULT;
WEAK void CAN1_TX_IRQHandler() ALIAS_DEFAULT;
WEAK void CAN1_RX0_IRQHandler() ALIAS_DEFAULT;
WEAK void CAN1_RX1_IRQHandler() ALIAS_DEFAULT;
WEAK void CAN1_SCE_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI9_5_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM1_BRK_TIM9_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM1_UP_TIM10_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM1_TRG_COM_TIM11_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM1_CC_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM2_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM3_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM4_IRQHandler() ALIAS_DEFAULT;
WEAK void I2C1_EV_IRQHandler() ALIAS_DEFAULT;
WEAK void I2C1_ER_IRQHandler() ALIAS_DEFAULT;
WEAK void I2C2_EV_IRQHandler() ALIAS_DEFAULT;
WEAK void I2C2_ER_IRQHandler() ALIAS_DEFAULT;
WEAK void SPI1_IRQHandler() ALIAS_DEFAULT;
WEAK void SPI2_IRQHandler() ALIAS_DEFAULT;
WEAK void USART1_IRQHandler() ALIAS_DEFAULT;
WEAK void USART2_IRQHandler() ALIAS_DEFAULT;
WEAK void USART3_IRQHandler() ALIAS_DEFAULT;
WEAK void EXTI15_10_IRQHandler() ALIAS_DEFAULT;
WEAK void RTC_Alarm_IRQHandler() ALIAS_DEFAULT;
WEAK void OTG_FS_WKUP_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM8_BRK_TIM12_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM8_UP_TIM13_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM8_TRG_COM_TIM14_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM8_CC_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA1_Stream7_IRQHandler() ALIAS_DEFAULT;
WEAK void FMC_IRQHandler() ALIAS_DEFAULT;
WEAK void SDMMC1_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM5_IRQHandler() ALIAS_DEFAULT;
WEAK void SPI3_IRQHandler() ALIAS_DEFAULT;
WEAK void UART4_IRQHandler() ALIAS_DEFAULT;
WEAK void UART5_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM6_DAC_IRQHandler() ALIAS_DEFAULT;
WEAK void TIM7_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream0_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream1_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream2_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream3_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream4_IRQHandler() ALIAS_DEFAULT;
WEAK void OTG_FS_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream5_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream6_IRQHandler() ALIAS_DEFAULT;
WEAK void DMA2_Stream7_IRQHandler() ALIAS_DEFAULT;
WEAK void USART6_IRQHandler() ALIAS_DEFAULT;
WEAK void I2C3_EV_IRQHandler() ALIAS_DEFAULT;
WEAK void I2C3_ER_IRQHandler() ALIAS_DEFAULT;
WEAK void OTG_HS_EP1_OUT_IRQHandler() ALIAS_DEFAULT;
WEAK void OTG_HS_EP1_IN_IRQHandler() ALIAS_DEFAULT;
WEAK void OTG_HS_WKUP_IRQHandler() ALIAS_DEFAULT;
WEAK void OTG_HS_IRQHandler() ALIAS_DEFAULT;
WEAK void RNG_IRQHandler() ALIAS_DEFAULT;
WEAK void FPU_IRQHandler() ALIAS_DEFAULT;
WEAK void UART7_IRQHandler() ALIAS_DEFAULT;
WEAK void UART8_IRQHandler() ALIAS_DEFAULT;
WEAK void SPI4_IRQHandler() ALIAS_DEFAULT;
WEAK void SPI5_IRQHandler() ALIAS_DEFAULT;
WEAK void SAI1_IRQHandler() ALIAS_DEFAULT;
WEAK void SAI2_IRQHandler() ALIAS_DEFAULT;
WEAK void QUADSPI_IRQHandler() ALIAS_DEFAULT;
WEAK void LPTIM1_IRQHandler() ALIAS_DEFAULT;
WEAK void SDMMC2_IRQHandler() ALIAS_DEFAULT;

#ifdef BOOTLOADER_FLASH
// Reduced size IRQ vectors for flash bootloader with interrupts disabled
const void * const irq_vectors[] SECTION(.fw_header) ALIGNED(512) USED = {
    &__stack_end,
    &Reset_Handler,
    &NMI_Handler,
    &HardFault_Handler,
};
#else
const void * const irq_vectors[] ALIGNED(512) USED = {
    &__stack_end,
    &Reset_Handler,
    &NMI_Handler,
    &HardFault_Handler,

    &MemManage_Handler,
    &BusFault_Handler,
    &UsageFault_Handler,
    0,
    0,
    0,
    0,
    &SVC_Handler,
    &DebugMon_Handler,
    0,
    &PendSV_Handler,
    &SysTick_Handler,

    &WWDG_IRQHandler,
    &PVD_IRQHandler,
    &TAMP_STAMP_IRQHandler,
    &RTC_WKUP_IRQHandler,
    &FLASH_IRQHandler,
    &RCC_IRQHandler,
    &EXTI0_IRQHandler,
    &EXTI1_IRQHandler,
    &EXTI2_IRQHandler,
    &EXTI3_IRQHandler,
    &EXTI4_IRQHandler,
    &DMA1_Stream0_IRQHandler,
    &DMA1_Stream1_IRQHandler,
    &DMA1_Stream2_IRQHandler,
    &DMA1_Stream3_IRQHandler,
    &DMA1_Stream4_IRQHandler,
    &DMA1_Stream5_IRQHandler,
    &DMA1_Stream6_IRQHandler,
    &ADC_IRQHandler,
    &CAN1_TX_IRQHandler,
    &CAN1_RX0_IRQHandler,
    &CAN1_RX1_IRQHandler,
    &CAN1_SCE_IRQHandler,
    &EXTI9_5_IRQHandler,
    &TIM1_BRK_TIM9_IRQHandler,
    &TIM1_UP_TIM10_IRQHandler,
    &TIM1_TRG_COM_TIM11_IRQHandler,
    &TIM1_CC_IRQHandler,
    &TIM2_IRQHandler,
    &TIM3_IRQHandler,
    &TIM4_IRQHandler,
    &I2C1_EV_IRQHandler,
    &I2C1_ER_IRQHandler,
    &I2C2_EV_IRQHandler,
    &I2C2_ER_IRQHandler,
    &SPI1_IRQHandler,
    &SPI2_IRQHandler,
    &USART1_IRQHandler,
    &USART2_IRQHandler,
    &USART3_IRQHandler,
    &EXTI15_10_IRQHandler,
    &RTC_Alarm_IRQHandler,
    &OTG_FS_WKUP_IRQHandler,
    &TIM8_BRK_TIM12_IRQHandler,
    &TIM8_UP_TIM13_IRQHandler,
    &TIM8_TRG_COM_TIM14_IRQHandler,
    &TIM8_CC_IRQHandler,
    &DMA1_Stream7_IRQHandler,
    &FMC_IRQHandler,
    &SDMMC1_IRQHandler,
    &TIM5_IRQHandler,
    &SPI3_IRQHandler,
    &UART4_IRQHandler,
    &UART5_IRQHandler,
    &TIM6_DAC_IRQHandler,
    &TIM7_IRQHandler,
    &DMA2_Stream0_IRQHandler,
    &DMA2_Stream1_IRQHandler,
    &DMA2_Stream2_IRQHandler,
    &DMA2_Stream3_IRQHandler,
    &DMA2_Stream4_IRQHandler,
    0,
    0,
    0,
    0,
    0,
    0,
    &OTG_FS_IRQHandler,
    &DMA2_Stream5_IRQHandler,
    &DMA2_Stream6_IRQHandler,
    &DMA2_Stream7_IRQHandler,
    &USART6_IRQHandler,
    &I2C3_EV_IRQHandler,
    &I2C3_ER_IRQHandler,
    &OTG_HS_EP1_OUT_IRQHandler,
    &OTG_HS_EP1_IN_IRQHandler,
    &OTG_HS_WKUP_IRQHandler,
    &OTG_HS_IRQHandler,
    0,
    0,
    &RNG_IRQHandler,
    &FPU_IRQHandler,
    &UART7_IRQHandler,
    &UART8_IRQHandler,
    &SPI4_IRQHandler,
    &SPI5_IRQHandler,
    0,
    &SAI1_IRQHandler,
    0,
    0,
    0,
    &SAI2_IRQHandler,
    &QUADSPI_IRQHandler,
    &LPTIM1_IRQHandler,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    &SDMMC2_IRQHandler,
};
#endif
