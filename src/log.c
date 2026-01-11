#include "stm32f7xx.h"
#include "macros.h"
#include "systick.h"
#include "log.h"

#define LOG_BUFFER_SIZE 256

typedef struct {
    log_type_t ev;
    // uint32_t ms;
    uint32_t data;
} log_entry_t;

static volatile struct {
    log_entry_t entry[LOG_BUFFER_SIZE];
    uint16_t wptr;
} log USED ALIGNED(16);

void log_push(log_type_t type, uint32_t data)
{
    // uint32_t t = systick_log();
    // uint32_t ms = systick_ms();
    __disable_irq();
    uint16_t wptr = log.wptr;
    log.wptr = (wptr + 1) % LOG_BUFFER_SIZE;
    __enable_irq();

    log_entry_t ev;
    ev.ev = type;
    // ev.ms = ms;
    ev.data = data;
    log.entry[wptr] = ev;
}
