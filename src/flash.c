#include "stm32f7xx.h"
#include "macros.h"
#include "flash.h"
#include "bootloader.h"
#include "semihosting.h"
#include "irq.h"
#include "log.h"

#ifndef BOOTLOADER
#error Only used in USB DFU bootloader
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

    // Total valid data length for dumping flash
    uint32_t fw_len;

    // Header bytes to be written last
    uint32_t fw_header;
    // UF2 block progress
    volatile uint16_t uf2_ofs;
    // Flash operation state
    volatile flash_state_t state;

    volatile bool abort;
    struct {
        bool erased;
    } sector[FLASH_SECTOR_TOTAL];
} flash;

static const struct {
    uint32_t start;
    uint32_t size;
} flash_sectors[FLASH_SECTOR_TOTAL] = {
    {0x08000000, 1024 * 16},
    {0x08004000, 1024 * 16},
    {0x08008000, 1024 * 16},
    {0x0800C000, 1024 * 16},
    {0x08010000, 1024 * 64},
    {0x08020000, 1024 * 128},
    {0x08040000, 1024 * 128},
    {0x08060000, 1024 * 128},
};

static const uint32_t uf2_data_size = sizeof(flash.uf2.data);
static const uint32_t flash_erased_pattern = 0xffffffff;

extern char __firmware_start;
extern char __firmware_end;

static void flash_memcpy(void *dst, const void *src, uint32_t len)
{
    uint32_t *dst32 = dst;
    const uint32_t *src32 = src;
    for (uint32_t cnt = 0; cnt < (len + 3) / 4; cnt++)
        dst32[cnt] = src32[cnt];
}

void flash_init()
{
    uint32_t pg = NVIC_GetPriorityGrouping();
    NVIC_SetPriority(FLASH_IRQn, NVIC_EncodePriority(pg, NvicPriorityFlash, 0));
    NVIC_EnableIRQ(FLASH_IRQn);
}

flash_state_t flash_state()
{
    return __atomic_load_n(&flash.state, __ATOMIC_RELAXED);
}

void flash_state_set(flash_state_t fstate)
{
    __atomic_store_n(&flash.state, fstate, __ATOMIC_RELAXED);
}

static bool flash_unlock()
{
    if (!(FLASH->CR & FLASH_CR_LOCK_Msk))
        return true;

    // Flash unlock sequence
    FLASH->KEYR = 0x45670123;
    FLASH->KEYR = 0xcdef89ab;

    bool locked = (FLASH->CR & FLASH_CR_LOCK_Msk) != 0;
    if (locked)
        flash_state_set(FlashUnlockError);
    return !locked;
}

static void flash_lock()
{
    if (FLASH->CR & FLASH_CR_LOCK_Msk)
        return;

    // Make sure flash operations are finished
    __atomic_store_n(&flash.abort, true, __ATOMIC_RELAXED);
    for (;;) {
        flash_state_t fstate = flash_state();
        if (fstate == FlashIdle || fstate >= FlashError)
            break;
    }
    FLASH->CR = FLASH_CR_LOCK_Msk;
    __atomic_store_n(&flash.abort, false, __ATOMIC_RELAXED);
}

static void flash_erase_sector(uint32_t sector)
{
    if (!flash_unlock())
        return;
    flash_state_set(FlashErase);
    flash.sector[sector].erased = true;
    FLASH->CR = (sector << FLASH_CR_SNB_Pos) | FLASH_CR_SER_Msk | FLASH_CR_STRT_Msk |
        (0b10 << FLASH_CR_PSIZE_Pos) |  // Valid for 3.3V
        FLASH_CR_RDERRIE_Msk | FLASH_CR_ERRIE_Msk | FLASH_CR_EOPIE_Msk;
}

static void flash_program(uint32_t sector, void *dst, uint32_t v)
{
    if (!flash_unlock())
        return;
    flash_state_set(flash_state() == FlashHeader ? FlashProgramHeader : FlashProgram);
    FLASH->CR = (sector << FLASH_CR_SNB_Pos) | FLASH_CR_PG_Msk |
        (0b10 << FLASH_CR_PSIZE_Pos) |  // Valid for 3.3V
        FLASH_CR_RDERRIE_Msk | FLASH_CR_ERRIE_Msk | FLASH_CR_EOPIE_Msk;
    *(volatile uint32_t *)dst = v;
}

static void flash_uf2_read_init()
{
    flash.uf2.magicStart0 = 0x0A324655;
    flash.uf2.magicStart1 = 0x9E5D5157;
    flash.uf2.flags = Uf2Flag_NotMainFlash | Uf2Flag_FamilyIDPresent | Uf2Flag_ExtensionTagsPresent;
    flash.uf2.targetAddr = 0;
    flash.uf2.payloadSize = 0;  // Extension tags only
    flash.uf2.blockNo = 0;
    flash.uf2.numBlocks = 1;
    flash.uf2.familyID = Uf2FamilyId;
    flash.uf2.magicEnd = 0x0AB16F30;

    uint32_t *p = (uint32_t *)&flash.uf2.data[0];
    for (uint32_t i = 0; i < sizeof(flash.uf2.data) / 4; i++)
        p[i] = 0;

    uint32_t fw_start = (uint32_t)&__firmware_start;
    uint32_t fw_last = (uint32_t)&__firmware_end - 4;
    while (fw_last >= fw_start && *(volatile uint32_t *)fw_last == flash_erased_pattern)
        fw_last -= 4;
    flash.fw_len = fw_last + 4 - fw_start;
    flash.uf2.numBlocks += (flash.fw_len + uf2_data_size - 1) / uf2_data_size;

    firmware_header_t *fw_hdr = (firmware_header_t *)fw_start;
    if (fw_hdr->header_size <= sizeof(firmware_header_t)) {
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

    uint32_t offset = (block - 1) * uf2_data_size;
    if (offset >= flash.fw_len)
        return 0;

    flash.uf2.flags = Uf2Flag_FamilyIDPresent;
    volatile uint32_t *src = (volatile uint32_t *)((uint32_t)&__firmware_start + offset);
    flash.uf2.targetAddr = (uint32_t)src;
    uint32_t block_size = flash.fw_len - offset;
    block_size = MIN(block_size, uf2_data_size);
    flash.uf2.payloadSize = block_size;
    flash.uf2.blockNo = block;
    uint32_t *dst = (uint32_t *)&flash.uf2.data[0];
    uint32_t i;
    for (i = 0; i < block_size / 4; i++)
        dst[i] = src[i];
    for (; i < uf2_data_size / 4; i++)
        dst[i] = flash_erased_pattern;
    return &flash.uf2;
}

void flash_uf2_write_init()
{
    if (flash_state() != FlashIdle)
        return;

    flash.fw_header = flash_erased_pattern;
    flash.abort = false;
    for (uint32_t i = 0; i < FLASH_SECTOR_TOTAL; i++) {
        flash.sector[i].erased = false;
        if (flash_sectors[i].start < (uint32_t)&__firmware_start)
            continue;
        // Check if sector is already erased
        flash.sector[i].erased = true;
        const volatile uint32_t *p = (volatile uint32_t *)flash_sectors[i].start;
        for (uint32_t ofs = 0; ofs < flash_sectors[i].size / 4; ofs++) {
            if (p[ofs] != flash_erased_pattern) {
                flash.sector[i].erased = false;
                break;
            }
        }
    }
}

static void flash_uf2_write_verify()
{
    const volatile uint32_t *src = (const uint32_t *)&flash.uf2.data[0];
    const volatile uint32_t *dst = (const uint32_t *)flash.uf2.targetAddr;
    for (uint32_t i = 0; i < (flash.uf2.payloadSize + 3) / 4; i++) {
        // Skip the header bytes for now
        if (dst == (uint32_t *)&__firmware_start && i == 0)
            continue;
        if (src[i] != dst[i]) {
            flash_state_set(FlashVerifyError);
            break;
        }
    }
}

static void flash_uf2_write_next()
{
    uint32_t uf2_ofs = 0;
    const volatile uint32_t *src = &flash.fw_header;
    uint32_t dst = (uint32_t)&__firmware_start;

    bool header = flash_state() == FlashHeader;
    if (!header) {
        uf2_ofs = __atomic_load_n(&flash.uf2_ofs, __ATOMIC_RELAXED);
        if (uf2_ofs >= flash.uf2.payloadSize) {
            // UF2 block completed
            flash_state_set(FlashIdle);
            flash_uf2_write_verify();
            return;
        }

        src = (const uint32_t *)&flash.uf2.data[uf2_ofs];
        dst = flash.uf2.targetAddr + uf2_ofs;
    }

    // Map dst to flash sector
    uint32_t sector = 0;
    for (uint32_t i = 0; i < FLASH_SECTOR_TOTAL; i++) {
        if (dst >= flash_sectors[i].start &&
            dst < flash_sectors[i].start + flash_sectors[i].size) {
            sector = i;
            break;
        }
    }
    if (!flash.sector[sector].erased) {
        // Sector need to be erased first
        flash_erase_sector(sector);
        return;
    }

    if (!header && dst == (uint32_t)&__firmware_start) {
        // Skip the header bytes for now
        flash.fw_header = *src;
        __atomic_store_n(&flash.uf2_ofs, uf2_ofs + 4, __ATOMIC_RELAXED);
        flash_uf2_write_next();
        return;
    }

    // Program flash
    __atomic_store_n(&flash.uf2_ofs, uf2_ofs + 4, __ATOMIC_RELAXED);
    flash_program(sector, (void *)dst, *src);
}

bool flash_uf2_write_block(uint8_t *data)
{
    flash_memcpy(&flash.uf2, data, FLASH_UF2_BLOCK_SIZE);

    // Block sanity check
    if (flash.uf2.magicStart0 != 0x0A324655)
        return false;
    if (flash.uf2.magicStart1 != 0x9E5D5157)
        return false;
    if (flash.uf2.magicEnd != 0x0AB16F30)
        return false;
    if (!(flash.uf2.flags & Uf2Flag_FamilyIDPresent))
        return false;
    if (flash.uf2.familyID != Uf2FamilyId)
        return false;
    if (!(flash.uf2.flags & Uf2Flag_NotMainFlash)) {
        if (flash.uf2.targetAddr < (uint32_t)&__firmware_start)
            return false;
        if (flash.uf2.targetAddr + flash.uf2.payloadSize > (uint32_t)&__firmware_end)
            return false;
    }
    if (flash.uf2.payloadSize > uf2_data_size)
        return false;

    if (flash.uf2.flags & Uf2Flag_NotMainFlash)
        return true;    // Not firmware data, ignored

    if (flash_state() != FlashIdle)
        return false;

    __atomic_store_n(&flash.uf2_ofs, 0, __ATOMIC_RELAXED);
    flash_uf2_write_next();
    return true;
}

bool flash_uf2_write_finish()
{
    // If firmware is valid, write the header bytes last
    if (flash.fw_header == flash_erased_pattern)
        return false;

    flash_state_set(FlashHeader);
    flash_uf2_write_next();
    return true;
}

void flash_uf2_write_abort()
{
    flash_lock();
    flash_state_set(FlashIdle);
}

void FLASH_IRQHandler()
{
    uint32_t sr = FLASH->SR;
    bool eop = (sr & FLASH_SR_EOP_Msk) != 0;
    bool err = (sr & (FLASH_SR_RDERR_Msk | FLASH_SR_ERSERR_Msk |
        FLASH_SR_PGPERR_Msk | FLASH_SR_PGAERR_Msk |
        FLASH_SR_WRPERR_Msk | FLASH_SR_OPERR_Msk)) != 0;
    FLASH->SR = FLASH_SR_RDERR_Msk | FLASH_SR_ERSERR_Msk |
        FLASH_SR_PGPERR_Msk | FLASH_SR_PGAERR_Msk |
        FLASH_SR_WRPERR_Msk | FLASH_SR_OPERR_Msk | FLASH_SR_EOP_Msk;
    log_push(LogFlash_INT, sr);

    switch (flash_state()) {
    case FlashErase:
        if (err) {
            flash_state_set(FlashEraseError);
            return;
        } else if (eop) {
            flash_uf2_write_next();
            return;
        }
        break;

    case FlashProgram:
        if (err) {
            flash_state_set(FlashProgramError);
            return;
        } else if (eop) {
            flash_uf2_write_next();
            return;
        }
        break;

    case FlashProgramHeader:
        if (err) {
            flash_state_set(FlashProgramHeaderError);
            return;
        } else if (eop) {
            if (firmware_header->u32[0] != flash.fw_header)
                flash_state_set(FlashVerifyHeaderError);
            else
                flash_state_set(FlashIdle);
            return;
        }
        break;
    }

    PANIC("Unexpected state");
}
