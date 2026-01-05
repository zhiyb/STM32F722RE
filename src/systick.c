#include "stm32f7xx.h"
#include "systick.h"

// 1000 Hz
#define PERIOD  (216000000 / 8 / 1000)

static volatile uint32_t ms = 0;

void systick_init()
{
    // Configure SysTick to 1ms period
    SysTick->CTRL = 0;
    // SysTick->LOAD = SysTick->CALIB;
    SysTick->LOAD = PERIOD - 1;
    SysTick->VAL = 0;
    // SysTick interrupt enable not controlled by NVIC
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

uint32_t systick_ms()
{
    return ms;
}

uint32_t systick_log()
{
    uint32_t v = ms << 16;
    v |= PERIOD - 1 - SysTick->VAL;
    return v;
}

void SysTick_Handler()
{
    // No atomics support on this architecture
    // __atomic_add_fetch(&ms, 1, __ATOMIC_RELAXED);
    ms += 1;
}
