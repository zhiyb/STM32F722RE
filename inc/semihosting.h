#pragma once

// #define ENABLE_DEBUGGING

#ifdef ENABLE_DEBUGGING
static inline void dbg_bkpt()
{
    asm volatile ("bkpt 0");
}

static inline void dbg_puts(const char *str)
{
    register int cmd asm ("r0") = 0x04;   // SYS_WRITE0
    register const void *data asm ("r1") = str;
    asm volatile ("bkpt 0xab"
        : "=r" (cmd)
        : "r" (cmd), "r" (data)
    );
}

#define DBG_STR_(x) #x
#define DBG_STR(x) DBG_STR_(x)

#define DBG_BKPT(str)  do { \
    dbg_puts("DEBUG " __FILE__ ":" DBG_STR(__LINE__) " " str "\r\n"); \
    dbg_bkpt(); \
} while (0)

#else   // ENABLE_DEBUGGING
static inline void dbg_bkpt() {}
static inline void dbg_puts(const char *str) {}
#define DBG_BKPT(str)

#endif  // ENABLE_DEBUGGING

#define TODO()  DBG_BKPT("TODO")

#define PANIC(str)  do { \
    DBG_BKPT(str); \
    for (;;) dbg_bkpt(); \
} while (0)

void panic_init();
