#include "boot_flash_stm32.h"
#include "stm32f4xx_hal.h"

/* Build-time board assumption; physical VDD has not been measured. */
#if !defined(BOOT_BOARD_VDD_MV) || BOOT_BOARD_VDD_MV < 2700 || \
    BOOT_BOARD_VDD_MV > 3600
#error "Word programming requires a configured 2.7-3.6 V board supply"
#endif

_Static_assert(FLASH_SECTOR_4 == 4u && FLASH_SECTOR_5 == 5u &&
               FLASH_SECTOR_10 == 10u, "HAL Flash sector numbering changed");

volatile uint32_t boot_flash_error_code, boot_flash_error_address;

static bool allowed_word(uint32_t address)
{
    return boot_flash_range_inside(BOOT_REGION_METADATA, address, 4) ||
           boot_flash_range_inside(BOOT_REGION_ACTIVE, address, 4) ||
           boot_flash_range_inside(BOOT_REGION_BACKUP, address, 4) ||
           boot_flash_range_inside(BOOT_REGION_STAGING, address, 4);
}

static bool erase_sector(void *unused, uint8_t sector)
{
    (void)unused;
    if (sector < 5 || sector > 10) return false;
    uint32_t base, bytes;
    if (!boot_flash_sector_bounds(sector, &base, &bytes) ||
        HAL_FLASH_Unlock() != HAL_OK) {
        boot_flash_error_code = 1;
        boot_flash_error_address = sector;
        return false;
    }
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = sector;
    erase.NbSectors = 1;
    uint32_t failed_sector = 0xFFFFFFFFu;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &failed_sector);
    HAL_StatusTypeDef locked = HAL_FLASH_Lock();
    if (status != HAL_OK || locked != HAL_OK ||
        failed_sector != 0xFFFFFFFFu) {
        boot_flash_error_code = 2;
        boot_flash_error_address = failed_sector;
        return false;
    }
    const volatile uint32_t *p = (const volatile uint32_t *)base;
    for (uint32_t i = 0; i < bytes / 4u; ++i)
        if (p[i] != 0xFFFFFFFFu) {
            boot_flash_error_code = 3;
            boot_flash_error_address = base + i * 4u;
            return false;
        }
    return true;
}

static bool program_word(void *unused, uint32_t address, uint32_t word)
{
    (void)unused;
    if ((address & 3u) || !allowed_word(address)) {
        boot_flash_error_code = 4;
        boot_flash_error_address = address;
        return false;
    }
    uint32_t existing = *(const volatile uint32_t *)address;
    if ((existing & word) != word) {
        boot_flash_error_code = 5;
        boot_flash_error_address = address;
        return false;
    }
    if (HAL_FLASH_Unlock() != HAL_OK) {
        boot_flash_error_code = 6;
        boot_flash_error_address = address;
        return false;
    }
    HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                                 address, word);
    HAL_StatusTypeDef locked = HAL_FLASH_Lock();
    if (status != HAL_OK || locked != HAL_OK ||
        *(const volatile uint32_t *)address != word) {
        boot_flash_error_code = 7;
        boot_flash_error_address = address;
        return false;
    }
    return true;
}

static const uint8_t *map(void *unused, uint32_t address, uint32_t bytes)
{
    (void)unused;
    if (!bytes || address < BOOT_FLASH_BASE || address >= BOOT_FLASH_END ||
        bytes > BOOT_FLASH_END - address) return NULL;
    return (const uint8_t *)address;
}

BootFlashBackend boot_flash_stm32_backend(void)
{
    BootFlashBackend flash = {erase_sector, program_word, map, NULL};
    return flash;
}
