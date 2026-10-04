#ifndef BOOT_TEST_FLASH_H
#define BOOT_TEST_FLASH_H

#include "boot_flash.h"

typedef struct {
    uint8_t bytes[BOOT_FLASH_END - BOOT_FLASH_BASE];
    uint64_t operations, fail_at;
    uint32_t erases, programs;
} BootTestFlash;

void boot_test_flash_init(BootTestFlash *model);
BootFlashBackend boot_test_flash_backend(BootTestFlash *model);

#endif
