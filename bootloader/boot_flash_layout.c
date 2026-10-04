#include "boot_flash_layout.h"

static const BootFlashRegion regions[] = {
    {BOOT_REGION_BOOT_BASE, BOOT_REGION_BOOT_BYTES, 0, 4},
    {BOOT_REGION_METADATA_BASE, BOOT_REGION_METADATA_BYTES, 4, 1},
    {BOOT_REGION_ACTIVE_BASE, BOOT_REGION_ACTIVE_BYTES, 5, 2},
    {BOOT_REGION_BACKUP_BASE, BOOT_REGION_BACKUP_BYTES, 7, 2},
    {BOOT_REGION_STAGING_BASE, BOOT_REGION_STAGING_BYTES, 9, 2},
    {BOOT_REGION_SPARE_BASE, BOOT_REGION_SPARE_BYTES, 11, 1}
};

static const uint32_t sector_start[BOOT_FLASH_SECTORS + 1] = {
    0x08000000u, 0x08004000u, 0x08008000u, 0x0800C000u,
    0x08010000u, 0x08020000u, 0x08040000u, 0x08060000u,
    0x08080000u, 0x080A0000u, 0x080C0000u, 0x080E0000u,
    0x08100000u
};

BootFlashRegion boot_flash_region(BootRegionId id)
{
    if ((unsigned)id >= sizeof(regions) / sizeof(regions[0])) {
        BootFlashRegion empty = {0};
        return empty;
    }
    return regions[id];
}

bool boot_flash_sector_bounds(uint8_t sector, uint32_t *base,
                              uint32_t *bytes)
{
    if (sector >= BOOT_FLASH_SECTORS || !base || !bytes) return false;
    *base = sector_start[sector];
    *bytes = sector_start[sector + 1] - sector_start[sector];
    return true;
}

bool boot_flash_range_inside(BootRegionId region, uint32_t address,
                              uint32_t bytes)
{
    BootFlashRegion r = boot_flash_region(region);
    return r.bytes && bytes && address >= r.base &&
           address - r.base < r.bytes && bytes <= r.bytes - (address - r.base);
}

bool boot_flash_layout_valid(void)
{
    if (sector_start[0] != BOOT_FLASH_BASE ||
        sector_start[BOOT_FLASH_SECTORS] != BOOT_FLASH_END) return false;
    for (unsigned i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        BootFlashRegion r = regions[i];
        if (r.first_sector + r.sectors > BOOT_FLASH_SECTORS ||
            r.base != sector_start[r.first_sector] ||
            r.base + r.bytes != sector_start[r.first_sector + r.sectors])
            return false;
    }
    return true;
}
