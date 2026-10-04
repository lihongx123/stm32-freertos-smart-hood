#include "boot_flash.h"
#include <string.h>

const uint8_t *boot_flash_map(const BootFlashBackend *flash,
                              uint32_t address, uint32_t bytes)
{
    if (!flash || !flash->map || !bytes || address < BOOT_FLASH_BASE ||
        address >= BOOT_FLASH_END || bytes > BOOT_FLASH_END - address)
        return NULL;
    return flash->map(flash->context, address, bytes);
}

static bool image_region(BootRegionId region)
{
    return region == BOOT_REGION_ACTIVE || region == BOOT_REGION_BACKUP ||
           region == BOOT_REGION_STAGING;
}

bool boot_flash_erase_image(const BootFlashBackend *flash,
                            BootRegionId region, uint32_t image_bytes)
{
    BootFlashRegion r = boot_flash_region(region);
    if (!flash || !flash->erase_sector || !image_region(region) ||
        !image_bytes || image_bytes > r.bytes) return false;
    for (uint8_t i = 0; i < r.sectors; ++i) {
        uint32_t start, size;
        if (!boot_flash_sector_bounds(r.first_sector + i, &start, &size))
            return false;
        if (start - r.base >= image_bytes) break;
        if (!flash->erase_sector(flash->context, r.first_sector + i))
            return false;
    }
    return true;
}

bool boot_flash_program_image(const BootFlashBackend *flash,
                              BootRegionId region, uint32_t offset,
                              const uint8_t *data, uint32_t bytes)
{
    BootFlashRegion r = boot_flash_region(region);
    if (!flash || !flash->program_word || !image_region(region) || !data ||
        (offset & 3u) || (bytes & 3u) || !bytes ||
        offset >= r.bytes || bytes > r.bytes - offset) return false;
    for (uint32_t i = 0; i < bytes; i += 4) {
        uint32_t word = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) |
                        ((uint32_t)data[i + 2] << 16) |
                        ((uint32_t)data[i + 3] << 24);
        uint32_t address = r.base + offset + i;
        if (!flash->program_word(flash->context, address, word)) return false;
        const uint8_t *verified = boot_flash_map(flash, address, 4);
        if (!verified || memcmp(verified, data + i, 4) != 0) return false;
    }
    return true;
}

bool boot_flash_copy_image(const BootFlashBackend *flash,
                           BootRegionId from, BootRegionId to,
                           uint32_t bytes)
{
    BootFlashRegion source = boot_flash_region(from);
    if (!image_region(from) || !image_region(to) || from == to ||
        !bytes || (bytes & 3u) || bytes > source.bytes ||
        bytes > boot_flash_region(to).bytes) return false;
    /* Do not retain a pointer across a Flash program operation. */
    for (uint32_t offset = 0; offset < bytes; offset += 4) {
        const uint8_t *p = boot_flash_map(flash, source.base + offset, 4);
        if (!p) return false;
        uint8_t word[4];
        memcpy(word, p, 4);
        if (!boot_flash_program_image(flash, to, offset, word, 4))
            return false;
    }
    return true;
}

bool boot_flash_program_metadata_word(const BootFlashBackend *flash,
                                      uint32_t offset, uint32_t word)
{
    if (!flash || !flash->program_word || (offset & 3u) ||
        offset >= BOOT_REGION_METADATA_BYTES ||
        4u > BOOT_REGION_METADATA_BYTES - offset) return false;
    uint32_t address = BOOT_REGION_METADATA_BASE + offset;
    if (!flash->program_word(flash->context, address, word)) return false;
    const uint8_t *verified = boot_flash_map(flash, address, 4);
    return verified && verified[0] == (uint8_t)word &&
           verified[1] == (uint8_t)(word >> 8) &&
           verified[2] == (uint8_t)(word >> 16) &&
           verified[3] == (uint8_t)(word >> 24);
}
