#include "stm32f7xx.h"
#include "semihosting.h"
#include "irq.h"

void panic_init()
{
    // Enable Usage Fault, Bus Fault, Memory Management Fault
    // Runtime errors are traped by undefined instructions
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
}

void HardFault_Handler()
{
    PANIC("HardFault");
}

void MemManage_Handler()
{
    PANIC("MemManage");
}

void BusFault_Handler()
{
    PANIC("BusFault");
}

void UsageFault_Handler()
{
    PANIC("UsageFault");
}
