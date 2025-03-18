#include "stm32c0xx.h"
#include "macros.h"
#include "systick.h"
#include "log.h"

#define LOG_BUFFER_SIZE     1024

typedef struct {
    uint8_t ev;
    uint8_t ms;
    uint16_t tick;
    uint32_t data;
} log_entry_t;

static volatile struct {
    log_entry_t entry[LOG_BUFFER_SIZE];
    uint16_t wptr;
} log USED ALIGNED(16);

void log_push(log_type_t type, uint32_t data)
{
    uint32_t t = systick_log();
    __disable_irq();
    uint16_t wptr = log.wptr;
    log.wptr = wptr + 1;
    __enable_irq();

    log_entry_t ev;
    ev.ev = type;
    ev.ms = t >> 16;
    ev.tick = t;
    ev.data = data;
    log.entry[wptr % LOG_BUFFER_SIZE] = ev;
}
