#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FLASH_UF2_BLOCK_SIZE    512

typedef enum {
    // https://github.com/microsoft/uf2/blob/master/utils/uf2families.json
    Uf2FamilyId_STM32F1 = 0x5ee21072,
    Uf2FamilyId_STM32F4 = 0x57755a57,
    Uf2FamilyId_STM32F7 = 0x53b80f00,

    // Family ID of our firmware
    Uf2FamilyId = Uf2FamilyId_STM32F7,
} uf2_family_id_t;

// Flash operation state
typedef enum {
    FlashIdle = 0,
    FlashHeader,
    FlashErase,
    FlashProgram,
    FlashProgramHeader,

    FlashError,
    FlashUnlockError,
    FlashEraseError,
    FlashProgramError,
    FlashProgramHeaderError,
    FlashVerifyError,
    FlashVerifyHeaderError,
} flash_state_t;

void flash_init();
flash_state_t flash_state();

const void *flash_uf2_read_block(uint32_t block);
void flash_uf2_write_init();
bool flash_uf2_write_block(uint8_t *data);
bool flash_uf2_write_finish();
void flash_uf2_write_abort();
