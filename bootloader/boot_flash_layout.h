#ifndef BOOT_FLASH_LAYOUT_H
#define BOOT_FLASH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/* STM32F407VGT6, 1 MiB single-bank flash; end addresses are exclusive. */
enum {
    BOOT_FLASH_BASE = 0x08000000u,
    BOOT_FLASH_END = 0x08100000u,
    BOOT_REGION_BOOT_BASE = 0x08000000u,
    BOOT_REGION_BOOT_BYTES = 64u * 1024u,
    BOOT_REGION_METADATA_BASE = 0x08010000u,
    BOOT_REGION_METADATA_BYTES = 64u * 1024u,
    BOOT_REGION_ACTIVE_BASE = 0x08020000u,
    BOOT_REGION_ACTIVE_BYTES = 256u * 1024u,
    BOOT_REGION_BACKUP_BASE = 0x08060000u,
    BOOT_REGION_BACKUP_BYTES = 256u * 1024u,
    BOOT_REGION_STAGING_BASE = 0x080A0000u,
    BOOT_REGION_STAGING_BYTES = 256u * 1024u,
    BOOT_REGION_SPARE_BASE = 0x080E0000u,
    BOOT_REGION_SPARE_BYTES = 128u * 1024u,
    BOOT_FLASH_SECTORS = 12u
};

_Static_assert(BOOT_REGION_BOOT_BASE + BOOT_REGION_BOOT_BYTES ==
               BOOT_REGION_METADATA_BASE, "boot/metadata overlap");
_Static_assert(BOOT_REGION_METADATA_BASE + BOOT_REGION_METADATA_BYTES ==
               BOOT_REGION_ACTIVE_BASE, "metadata/active overlap");
_Static_assert(BOOT_REGION_ACTIVE_BASE + BOOT_REGION_ACTIVE_BYTES ==
               BOOT_REGION_BACKUP_BASE, "active/backup overlap");
_Static_assert(BOOT_REGION_BACKUP_BASE + BOOT_REGION_BACKUP_BYTES ==
               BOOT_REGION_STAGING_BASE, "backup/staging overlap");
_Static_assert(BOOT_REGION_STAGING_BASE + BOOT_REGION_STAGING_BYTES ==
               BOOT_REGION_SPARE_BASE, "staging/spare overlap");
_Static_assert(BOOT_REGION_SPARE_BASE + BOOT_REGION_SPARE_BYTES ==
               BOOT_FLASH_END, "region map exceeds 1 MiB flash");

typedef enum {
    BOOT_REGION_BOOT,
    BOOT_REGION_METADATA,
    BOOT_REGION_ACTIVE,
    BOOT_REGION_BACKUP,
    BOOT_REGION_STAGING,
    BOOT_REGION_SPARE
} BootRegionId;

typedef struct {
    uint32_t base, bytes;
    uint8_t first_sector, sectors;
} BootFlashRegion;

BootFlashRegion boot_flash_region(BootRegionId id);
bool boot_flash_sector_bounds(uint8_t sector, uint32_t *base,
                              uint32_t *bytes);
bool boot_flash_range_inside(BootRegionId region, uint32_t address,
                              uint32_t bytes);
bool boot_flash_layout_valid(void);

#endif
