#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include "boot_flash_layout.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MCU adapter and deterministic host model implement these same operations. */
typedef struct {
    bool (*erase_sector)(void *context, uint8_t sector);
    bool (*program_word)(void *context, uint32_t address, uint32_t word);
    const uint8_t *(*map)(void *context, uint32_t address, uint32_t bytes);
    void *context;
} BootFlashBackend;

const uint8_t *boot_flash_map(const BootFlashBackend *flash,
                              uint32_t address, uint32_t bytes);
bool boot_flash_erase_image(const BootFlashBackend *flash,
                            BootRegionId region, uint32_t image_bytes);
bool boot_flash_program_image(const BootFlashBackend *flash,
                              BootRegionId region, uint32_t offset,
                              const uint8_t *data, uint32_t bytes);
bool boot_flash_copy_image(const BootFlashBackend *flash,
                           BootRegionId from, BootRegionId to,
                           uint32_t bytes);
bool boot_flash_program_metadata_word(const BootFlashBackend *flash,
                                      uint32_t offset, uint32_t word);

#endif
