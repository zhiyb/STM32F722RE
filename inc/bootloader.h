#pragma once
#include <stdint.h>

typedef enum {
    BootloaderBootFw = 0,
    BootloaderRun,
    BootloaderRunItcm,
    BootloaderUsbDfu,
    BootloaderButton,
} bootloader_op_t;

typedef struct {
    uint32_t op;
    union {
        uint32_t param;
        void *ptr;
    };
} bootloader_req_t;

extern char __bootloader_req;
static volatile bootloader_req_t * const bootloader_req = (bootloader_req_t *)&__bootloader_req;

typedef union {
    uint8_t data[256];
    uint32_t u32[];
    struct {
        uint32_t header_size;
        uint32_t version;
        void (*entry)();
        uint32_t ext_tag[61];
    };
} firmware_header_t;

extern char __firmware_start;
static const firmware_header_t * const firmware_header = (firmware_header_t *)&__firmware_start;
