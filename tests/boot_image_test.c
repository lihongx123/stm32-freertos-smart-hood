#include "boot_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_le32(uint8_t *dst, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) dst[i] = (uint8_t)(value >> (8u * i));
}

static void verify_linked_image(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 8 && length <= BOOT_APP_SLOT_BYTES &&
           (length & 3) == 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    uint8_t *image = malloc((size_t)length);
    assert(image);
    assert(fread(image, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    BootImageHeader header = {BOOT_IMAGE_MAGIC, BOOT_IMAGE_FORMAT, 1u,
                              (uint32_t)length,
                              boot_crc32(image, (size_t)length), 0};
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, (size_t)length) == BOOT_IMAGE_OK);
    printf("PASS linked F407 image vector/CRC, bytes=%ld\n", length);
    free(image);
}

int main(int argc, char **argv)
{
    assert(boot_crc32("123456789", 9) == 0xCBF43926u);
    uint8_t image[64] = {0};
    put_le32(image, 0x2001FFF8u);
    put_le32(image + 4, BOOT_APP_BASE + 0x21u);
    for (size_t i = 8; i < sizeof(image); ++i) image[i] = (uint8_t)i;
    BootImageHeader header = {BOOT_IMAGE_MAGIC, BOOT_IMAGE_FORMAT, 3u,
                              sizeof(image), boot_crc32(image, sizeof(image)), 0};
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_OK);
    assert(boot_image_validate(0, image, sizeof(image)) == BOOT_IMAGE_BAD_ARGUMENT);
    assert(boot_image_validate(&header, 0, sizeof(image)) == BOOT_IMAGE_BAD_ARGUMENT);
    assert(boot_image_validate(&header, image, sizeof(image) - 1) == BOOT_IMAGE_BAD_SIZE);
    header.image_bytes = BOOT_APP_SLOT_BYTES + 4u;
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_SIZE);
    header.image_bytes = sizeof(image);
    header.header_crc32 = boot_header_crc32(&header);
    header.format++;
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_HEADER);
    header.format = BOOT_IMAGE_FORMAT;
    header.firmware_version++;
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_HEADER);
    header.firmware_version--;
    header.header_crc32 = boot_header_crc32(&header);
    image[10] ^= 1u;
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_CRC);
    image[10] ^= 1u;

    put_le32(image, 0x20020004u);
    header.image_crc32 = boot_crc32(image, sizeof(image));
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_STACK);
    put_le32(image, 0x2001FFF8u);
    put_le32(image + 4, BOOT_APP_BASE + 0x20u); /* Thumb bit absent. */
    header.image_crc32 = boot_crc32(image, sizeof(image));
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_RESET_VECTOR);
    put_le32(image + 4, BOOT_APP_BASE + 0x101u); /* Outside image length. */
    header.image_crc32 = boot_crc32(image, sizeof(image));
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_BAD_RESET_VECTOR);
    put_le32(image + 4, BOOT_APP_BASE + 0x21u);
    put_le32(image, 0x1000FFF8u); /* F407 CCM SRAM is CPU-accessible. */
    header.image_crc32 = boot_crc32(image, sizeof(image));
    header.header_crc32 = boot_header_crc32(&header);
    assert(boot_image_validate(&header, image, sizeof(image)) == BOOT_IMAGE_OK);
    puts("PASS boot CRC, header, bounds, corrupt image and vector validation");
    if (argc > 1) verify_linked_image(argv[1]);
    return 0;
}
