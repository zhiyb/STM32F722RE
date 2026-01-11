#include "stm32f7xx.h"
#include "semihosting.h"
#include "irq.h"

void panic_init()
{
	uint32_t pg = NVIC_GetPriorityGrouping();

    // Enable Memory Management Fault
    NVIC_SetPriority(MemoryManagement_IRQn, NVIC_EncodePriority(pg, NvicPriorityFault, 0));
    NVIC_EnableIRQ(MemoryManagement_IRQn);

    // Enable Bus Fault
    NVIC_SetPriority(BusFault_IRQn, NVIC_EncodePriority(pg, NvicPriorityFault, 0));
    NVIC_EnableIRQ(BusFault_IRQn);

    // Enable Usage Fault
    // Runtime errors are traped by undefined instructions
    NVIC_SetPriority(UsageFault_IRQn, NVIC_EncodePriority(pg, NvicPriorityFault, 0));
    NVIC_EnableIRQ(UsageFault_IRQn);
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
