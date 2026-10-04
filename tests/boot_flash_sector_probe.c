#include "boot_test_flash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static BootTestFlash model;

static void check_sector(uint8_t sector)
{
    uint32_t base, bytes;
    assert(boot_flash_sector_bounds(sector, &base, &bytes));
    const uint32_t offset = base - BOOT_FLASH_BASE;
    for (uint32_t i = 0; i < bytes; ++i)
        model.bytes[offset + i] = (uint8_t)((i * 13u + sector * 17u) & 0x7Fu);
    const uint8_t before_left = offset ? model.bytes[offset - 1] : 0;
    const uint8_t before_right = model.bytes[offset + bytes];
    BootFlashBackend backend = boot_test_flash_backend(&model);
    assert(backend.erase_sector(backend.context, sector));
    uint32_t bad = 0;
    for (uint32_t i = 0; i < bytes; ++i)
        if (model.bytes[offset + i] != 0xFFu) ++bad;
    assert(bad == 0);
    if (offset) assert(model.bytes[offset - 1] == before_left);
    assert(model.bytes[offset + bytes] == before_right);
    printf("sector=%u start=0x%08X size=%u bad_byte_count=%u pass=true\n",
           sector, base, bytes, bad);
}

int main(void)
{
    boot_test_flash_init(&model);
    assert(boot_flash_layout_valid());
    check_sector(0); /* protected in production, geometry/backend model only */
    check_sector(4); /* metadata geometry/backend model only */
    check_sector(5); /* exact Renode failure sector */
    check_sector(9); /* staging 128 KiB sector */
    BootFlashBackend backend = boot_test_flash_backend(&model);
    uint32_t base, bytes;
    assert(boot_flash_sector_bounds(5, &base, &bytes));
    assert(backend.program_word(backend.context, base, 0x11223344u));
    assert(memcmp(backend.map(backend.context, base, 4),
                  "\x44\x33\x22\x11", 4) == 0);
    assert(!backend.program_word(backend.context, base, 0xFFFFFFFFu));
    assert(!backend.program_word(backend.context, BOOT_FLASH_END, 0u));
    assert(!backend.erase_sector(backend.context, BOOT_FLASH_SECTORS));
    assert(model.bytes[BOOT_REGION_METADATA_BASE - BOOT_FLASH_BASE] == 0xFFu);
    puts("program_readback=PASS forbidden_write=PASS bounds=PASS metadata_unchanged=PASS");
    return 0;
}
