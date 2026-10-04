#include "boot_test_flash.h"
#include <string.h>

void boot_test_flash_init(BootTestFlash *model)
{
    memset(model, 0, sizeof(*model));
    memset(model->bytes, 0xFF, sizeof(model->bytes));
}

static bool erase_sector(void *context, uint8_t sector)
{
    BootTestFlash *model = context;
    uint32_t base, bytes;
    if (!boot_flash_sector_bounds(sector, &base, &bytes)) return false;
    ++model->operations;
    uint8_t *p = model->bytes + base - BOOT_FLASH_BASE;
    if (model->fail_at && model->operations == model->fail_at) {
        memset(p, 0xFF, bytes / 2u); /* interrupted erase, not atomic */
        return false;
    }
    memset(p, 0xFF, bytes);
    ++model->erases;
    return true;
}

static bool program_word(void *context, uint32_t address, uint32_t word)
{
    BootTestFlash *model = context;
    if ((address & 3u) || address < BOOT_FLASH_BASE ||
        address >= BOOT_FLASH_END || 4u > BOOT_FLASH_END - address)
        return false;
    uint8_t *p = model->bytes + address - BOOT_FLASH_BASE;
    uint8_t bytes[4] = {(uint8_t)word, (uint8_t)(word >> 8),
                        (uint8_t)(word >> 16), (uint8_t)(word >> 24)};
    for (unsigned i = 0; i < 4; ++i)
        if ((p[i] & bytes[i]) != bytes[i]) return false;
    ++model->operations;
    if (model->fail_at && model->operations == model->fail_at) {
        p[0] &= bytes[0];
        p[1] &= bytes[1]; /* interrupted word, not atomic */
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) p[i] &= bytes[i];
    ++model->programs;
    return true;
}

static const uint8_t *map(void *context, uint32_t address, uint32_t bytes)
{
    BootTestFlash *model = context;
    if (!bytes || address < BOOT_FLASH_BASE || address >= BOOT_FLASH_END ||
        bytes > BOOT_FLASH_END - address) return NULL;
    return model->bytes + address - BOOT_FLASH_BASE;
}

BootFlashBackend boot_test_flash_backend(BootTestFlash *model)
{
    BootFlashBackend backend = {erase_sector, program_word, map, model};
    return backend;
}
