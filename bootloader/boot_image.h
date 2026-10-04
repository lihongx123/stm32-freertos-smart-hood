#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include "boot_flash_layout.h"

enum {
    BOOT_APP_BASE = BOOT_REGION_ACTIVE_BASE,
    BOOT_APP_SLOT_BYTES = BOOT_REGION_ACTIVE_BYTES,
    BOOT_IMAGE_MAGIC = 0x484F4F44u,
    BOOT_IMAGE_FORMAT = 1u
};

/* Transport header is separate from the raw image copied to BOOT_APP_BASE. */
typedef struct {
    uint32_t magic;
    uint32_t format;
    uint32_t firmware_version;
    uint32_t image_bytes;
    uint32_t image_crc32;
    uint32_t header_crc32;
} BootImageHeader;

typedef enum {
    BOOT_IMAGE_OK = 0,
    BOOT_IMAGE_BAD_ARGUMENT,
    BOOT_IMAGE_BAD_HEADER,
    BOOT_IMAGE_BAD_SIZE,
    BOOT_IMAGE_BAD_CRC,
    BOOT_IMAGE_BAD_STACK,
    BOOT_IMAGE_BAD_RESET_VECTOR
} BootImageResult;

uint32_t boot_crc32(const void *data, size_t bytes);
uint32_t boot_crc32_update(uint32_t state, const void *data, size_t bytes);
uint32_t boot_header_crc32(const BootImageHeader *header);
BootImageResult boot_header_validate(const BootImageHeader *header);
BootImageResult boot_vectors_validate(const BootImageHeader *header,
                                      uint32_t stack, uint32_t reset);
BootImageResult boot_image_validate(const BootImageHeader *header,
                                    const uint8_t *image, size_t available);

#endif
