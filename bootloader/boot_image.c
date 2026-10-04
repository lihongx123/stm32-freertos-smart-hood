#include "boot_image.h"

static uint32_t load_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void store_le32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (8u * i));
}

uint32_t boot_crc32_update(uint32_t state, const void *data, size_t bytes)
{
    const uint8_t *input = data;
    if (!input && bytes) return state;
    for (size_t i = 0; i < bytes; ++i) {
        state ^= input[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ ((state & 1u) ? 0xEDB88320u : 0u);
    }
    return state;
}

uint32_t boot_crc32(const void *data, size_t bytes)
{
    if (!data && bytes) return 0;
    return boot_crc32_update(0xFFFFFFFFu, data, bytes) ^ 0xFFFFFFFFu;
}

uint32_t boot_header_crc32(const BootImageHeader *header)
{
    if (!header) return 0;
    uint8_t fields[20];
    store_le32(fields, header->magic);
    store_le32(fields + 4, header->format);
    store_le32(fields + 8, header->firmware_version);
    store_le32(fields + 12, header->image_bytes);
    store_le32(fields + 16, header->image_crc32);
    return boot_crc32(fields, sizeof(fields));
}

BootImageResult boot_header_validate(const BootImageHeader *header)
{
    if (!header) return BOOT_IMAGE_BAD_ARGUMENT;
    if (header->magic != BOOT_IMAGE_MAGIC ||
        header->format != BOOT_IMAGE_FORMAT ||
        header->header_crc32 != boot_header_crc32(header))
        return BOOT_IMAGE_BAD_HEADER;
    if (header->image_bytes < 8u ||
        header->image_bytes > BOOT_APP_SLOT_BYTES ||
        (header->image_bytes & 3u) != 0u) return BOOT_IMAGE_BAD_SIZE;
    return BOOT_IMAGE_OK;
}

BootImageResult boot_vectors_validate(const BootImageHeader *header,
                                      uint32_t stack, uint32_t reset)
{
    BootImageResult result = boot_header_validate(header);
    if (result != BOOT_IMAGE_OK) return result;
    const int main_sram = stack > 0x20000000u && stack <= 0x20020000u;
    const int ccm_sram = stack > 0x10000000u && stack <= 0x10010000u;
    if ((stack & 7u) != 0u || (!main_sram && !ccm_sram))
        return BOOT_IMAGE_BAD_STACK;
    const uint32_t reset_address = reset & ~1u;
    if (!(reset & 1u) || reset_address < BOOT_APP_BASE ||
        reset_address >= BOOT_APP_BASE + header->image_bytes)
        return BOOT_IMAGE_BAD_RESET_VECTOR;
    return BOOT_IMAGE_OK;
}

BootImageResult boot_image_validate(const BootImageHeader *header,
                                    const uint8_t *image, size_t available)
{
    if (!header || !image) return BOOT_IMAGE_BAD_ARGUMENT;
    BootImageResult result = boot_header_validate(header);
    if (result != BOOT_IMAGE_OK) return result;
    if (header->image_bytes > available) return BOOT_IMAGE_BAD_SIZE;
    if (boot_crc32(image, header->image_bytes) != header->image_crc32)
        return BOOT_IMAGE_BAD_CRC;
    return boot_vectors_validate(header, load_le32(image),
                                 load_le32(image + 4));
}
