#pragma once

static inline void dbg_bkpt()
{
    asm volatile ("bkpt 0");
}

static inline void dbg_puts(const char *str)
{
    register void *cmd asm ("r0") = (void *)0x04;   // SYS_WRITE0
    register void *data asm ("r1") = str;
    asm volatile ("bkpt 0xab"
        : "=r" (data)
        : "r" (cmd), "r" (data)
    );
}
