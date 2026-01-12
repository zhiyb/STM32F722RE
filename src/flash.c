#include "stm32f7xx.h"
#include "macros.h"
#include "flash.h"

typedef union PACKED {
    uint8_t data[256];
    uint32_t u32[];
    struct PACKED {
        uint32_t header_size;
        uint32_t version;
        void (*entry)();
        uint32_t ext_tag[61];
    };
} flash_header_t;

#ifndef BOOTLOADER
extern void Reset_Handler();

static const flash_header_t fw_header USED SECTION(.fw_header) = {
    .header_size = 4 * 3,
    .version = -1,
    .entry = &Reset_Handler,
    .ext_tag = {0},
};
#endif

typedef enum {
    // not main flash
    // this block should be skipped when writing the device flash;
    // it can be used to store "comments" in the file, typically embedded source code
    // or debug info that does not fit on the device flash
    Uf2Flag_NotMainFlash = 0x00000001,
    // file container
    Uf2Flag_FileContainer = 0x00001000,
    // familyID present
    // when set, the fileSize/familyID holds a value identifying the board family
    // (usually corresponds to an MCU)
    Uf2Flag_FamilyIDPresent = 0x00002000,
    // MD5 checksum present
    Uf2Flag_MD5ChecksumPresent = 0x00004000,
    // extension tags present
    Uf2Flag_ExtensionTagsPresent = 0x00008000,
} uf2_flag_t;

typedef enum {
    // https://github.com/microsoft/uf2/blob/master/utils/uf2families.json
    Uf2FamilyId_STM32F1 = 0x5ee21072,
    Uf2FamilyId_STM32F4 = 0x57755a57,
    Uf2FamilyId_STM32F7 = 0x53b80f00,
} uf2_family_id_t;

typedef enum {
    // version of firmware file - UTF8 semver string
    Uf2ExtTag_Version = 0x9fc7bc00,
    // description of device for which the firmware file is destined (UTF8)
    Uf2ExtTag_Device = 0x650d9d00,
    // page size of target device (32 bit unsigned number)
    Uf2ExtTag_PageSize = 0x0be9f700,
    // SHA-2 checksum of firmware (can be of various size)
    Uf2ExtTag_SHA2Checksum = 0xb46db000,
    // device type identifier - a refinement of familyID meant to identify a kind of device
    // (eg., a toaster with specific pinout and heating unit), not only MCU;
    // 32 or 64 bit number; can be hash of 0x650d9d
    Uf2ExtTag_DeviceType = 0xc8a72900,
} uf2_ext_tag_t;

static struct {
    union ALIGNED(4) {
        uint8_t buf[512];

        // https://github.com/microsoft/uf2/blob/master/README.md
        struct PACKED {
            uint32_t magicStart0;
            uint32_t magicStart1;
            uint32_t flags;
            uint32_t targetAddr;
            uint32_t payloadSize;
            uint32_t blockNo;
            uint32_t numBlocks;
            union PACKED {
                uint32_t fileSize;
                uint32_t familyID;
            };
            uint8_t data[476];
            uint32_t magicEnd;
        } uf2;
    };
    uint32_t fw_len;
} flash;

static const uint32_t uf2_block_size = sizeof(flash.uf2.data);

extern char __firmware_start;
extern char __firmware_end;

static void flash_uf2_read_init()
{
    flash.uf2.magicStart0 = 0x0A324655;
    flash.uf2.magicStart1 = 0x9E5D5157;
    flash.uf2.flags = Uf2Flag_NotMainFlash | Uf2Flag_FamilyIDPresent | Uf2Flag_ExtensionTagsPresent;
    flash.uf2.targetAddr = 0;
    flash.uf2.payloadSize = 0;  // Extension tags only
    flash.uf2.blockNo = 0;
    flash.uf2.numBlocks = 1;
    flash.uf2.familyID = Uf2FamilyId_STM32F7;
    flash.uf2.magicEnd = 0x0AB16F30;

    uint32_t *p = (uint32_t *)&flash.uf2.data[0];
    for (uint32_t i = 0; i < sizeof(flash.uf2.data) / 4; i++)
        p[i] = 0;

    uint32_t fw_start = (uint32_t)&__firmware_start;
    uint32_t fw_last = (uint32_t)&__firmware_end - 4;
    while (fw_last >= fw_start && *(volatile uint32_t *)fw_last == 0xffffffff)
        fw_last -= 4;
    flash.fw_len = fw_last + 4 - fw_start;
    flash.uf2.numBlocks += (flash.fw_len + uf2_block_size - 1) / uf2_block_size;

    flash_header_t *fw_hdr = (flash_header_t *)fw_start;
    if (fw_hdr->header_size <= sizeof(flash_header_t)) {
        flash.uf2.payloadSize = fw_hdr->header_size;
        for (uint32_t i = 0; i < (fw_hdr->header_size + 3) / 4; i++)
            p[i] = fw_hdr->u32[i];
    }
}

const void *flash_uf2_read_block(uint32_t block)
{
    if (block == 0) {
        flash_uf2_read_init();
        return &flash.uf2;
    }

    uint32_t offset = (block - 1) * uf2_block_size;
    if (offset >= flash.fw_len)
        return 0;

    flash.uf2.flags = Uf2Flag_FamilyIDPresent;
    volatile uint32_t *src = (volatile uint32_t *)((uint32_t)&__firmware_start + offset);
    flash.uf2.targetAddr = (uint32_t)src;
    uint32_t block_size = flash.fw_len - offset;
    block_size = MIN(block_size, uf2_block_size);
    flash.uf2.payloadSize = block_size;
    flash.uf2.blockNo = block;
    uint32_t *dst = (uint32_t *)&flash.uf2.data[0];
    uint32_t i;
    for (i = 0; i < block_size / 4; i++)
        dst[i] = src[i];
    for (; i < uf2_block_size / 4; i++)
        dst[i] = 0xffffffff;
    return &flash.uf2;
}
