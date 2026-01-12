#include "stm32f7xx.h"
#include "macros.h"
#include "flash.h"

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
    uint32_t data_len;
} flash;

typedef union PACKED {
    uint8_t data[256];
    struct PACKED {
        uint32_t version;
        uint32_t ext_tag[];
    };
} flash_header_t;

static void flash_uf2_read_init()
{
    flash.uf2.magicStart0 = 0x0A324655;
    flash.uf2.magicStart1 = 0x9E5D5157;
    flash.uf2.flags = Uf2Flag_NotMainFlash | Uf2Flag_FamilyIDPresent | Uf2Flag_ExtensionTagsPresent;
    flash.uf2.targetAddr = 0;
    flash.uf2.payloadSize = 0;  // Extension tags only
    flash.uf2.blockNo = 0;
    flash.uf2.numBlocks = 1;    // TODO
    flash.uf2.familyID = Uf2FamilyId_STM32F7;
    flash.uf2.magicEnd = 0x0AB16F30;

    uint32_t *p = (uint32_t *)&flash.uf2.data[0];
    for (uint32_t i = 0; i < sizeof(flash.uf2.data) / 4; i++)
        p[i] = 0;
}

const void *flash_uf2_read_block(uint32_t block)
{
    if (block == 0) {
        flash_uf2_read_init();
        return &flash.uf2;
    }
    return 0;
}
