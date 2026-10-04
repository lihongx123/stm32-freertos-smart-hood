#include "boot_lifecycle.h"
#include "boot_test_flash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BootTestFlash model;
static BootLifecycle life;
static uint8_t *old_image, *new_image;
static uint32_t image_bytes, old_crc, new_crc;

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static bool seed_word(void *context, size_t offset, uint32_t value)
{
    BootFlashBackend *flash = context;
    return boot_flash_program_metadata_word(flash, (uint32_t)offset, value);
}

static void seed(void)
{
    boot_test_flash_init(&model);
    memset(model.bytes, 0xA5, BOOT_REGION_BOOT_BYTES);
    memcpy(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
           old_image, image_bytes);
    BootFlashBackend flash = boot_test_flash_backend(&model);
    BootRecord initial = {0};
    initial.sequence = 1;
    initial.state = BOOT_STATE_CONFIRMED;
    initial.active_version = 1;
    initial.active_bytes = image_bytes;
    initial.active_crc = old_crc;
    assert(boot_journal_append(model.bytes + BOOT_REGION_METADATA_BASE -
                               BOOT_FLASH_BASE, BOOT_JOURNAL_BYTES,
                               &initial, seed_word, &flash) == BOOT_JOURNAL_OK);
    boot_lifecycle_init(&life, flash);
    assert(life.has_record && life.latest.state == BOOT_STATE_CONFIRMED);
    assert(boot_flash_layout_valid());
}

static BootWireFrame start_frame(uint32_t bytes, uint32_t crc)
{
    BootImageHeader h = {BOOT_IMAGE_MAGIC, BOOT_IMAGE_FORMAT, 2,
                         bytes, crc, 0};
    h.header_crc32 = boot_header_crc32(&h);
    BootWireFrame f = {0};
    f.type = BOOT_FRAME_START;
    f.length = 24;
    put32(f.payload, h.magic);
    put32(f.payload + 4, h.format);
    put32(f.payload + 8, h.firmware_version);
    put32(f.payload + 12, h.image_bytes);
    put32(f.payload + 16, h.image_crc32);
    put32(f.payload + 20, h.header_crc32);
    return f;
}

static BootWireFrame chunk_frame(const uint8_t *image, uint16_t sequence)
{
    BootWireFrame f = {0};
    f.type = BOOT_FRAME_CHUNK;
    f.sequence = sequence;
    uint32_t offset = (uint32_t)sequence * BOOT_UPDATE_CHUNK_BYTES;
    f.length = image_bytes - offset > BOOT_UPDATE_CHUNK_BYTES ?
        BOOT_UPDATE_CHUNK_BYTES : (uint16_t)(image_bytes - offset);
    memcpy(f.payload, image + offset, f.length);
    return f;
}

static BootWireFrame end_frame(void)
{
    BootWireFrame f = {0};
    f.type = BOOT_FRAME_END;
    f.sequence = (uint16_t)((image_bytes + BOOT_UPDATE_CHUNK_BYTES - 1) /
                              BOOT_UPDATE_CHUNK_BYTES);
    return f;
}

static void transfer(void)
{
    BootWireFrame f = start_frame(image_bytes, new_crc);
    assert(boot_lifecycle_receive(&life, &f, 0) == BOOT_UPDATE_OK);
    uint16_t chunks = end_frame().sequence;
    for (uint16_t i = 0; i < chunks; ++i) {
        f = chunk_frame(new_image, i);
        assert(boot_lifecycle_receive(&life, &f, i + 1u) == BOOT_UPDATE_OK);
        if (i == 0) {
            assert(boot_lifecycle_receive(&life, &f, i + 2u) == BOOT_UPDATE_OK);
        }
    }
    f = end_frame();
    assert(boot_lifecycle_receive(&life, &f, chunks + 2u) == BOOT_UPDATE_OK);
    assert(life.stage_valid && life.transfer.complete);
    assert(memcmp(model.bytes + BOOT_REGION_STAGING_BASE - BOOT_FLASH_BASE,
                  new_image, image_bytes) == 0);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
}

static void assert_boot_protected(void)
{
    for (uint32_t i = 0; i < BOOT_REGION_BOOT_BYTES; ++i)
        assert(model.bytes[i] == 0xA5);
}

static void normal_and_rollback(void)
{
    seed();
    transfer();
    assert(boot_lifecycle_activate(&life) == BOOT_LIFECYCLE_OK);
    assert(life.latest.state == BOOT_STATE_PENDING);
    assert(memcmp(model.bytes + BOOT_REGION_BACKUP_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  new_image, image_bytes) == 0);
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(life.latest.attempts == 1);
    assert(boot_lifecycle_confirm(&life) == BOOT_LIFECYCLE_OK);
    assert(life.latest.state == BOOT_STATE_CONFIRMED);
    assert(memcmp(model.bytes + BOOT_REGION_BACKUP_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
    assert_boot_protected();

    seed();
    transfer();
    assert(boot_lifecycle_activate(&life) == BOOT_LIFECYCLE_OK);
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(life.latest.attempts == BOOT_PENDING_MAX_ATTEMPTS);
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(life.latest.state == BOOT_STATE_CONFIRMED);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
    assert(boot_lifecycle_validate(&life, BOOT_REGION_ACTIVE, 1,
                                    image_bytes, old_crc));
    assert_boot_protected();
}

static void transfer_failures(void)
{
    seed();
    BootWireFrame f = start_frame(image_bytes, new_crc ^ 1u);
    assert(boot_lifecycle_receive(&life, &f, 0) == BOOT_UPDATE_OK);
    uint16_t chunks = end_frame().sequence;
    for (uint16_t i = 0; i < chunks; ++i) {
        f = chunk_frame(new_image, i);
        assert(boot_lifecycle_receive(&life, &f, i + 1u) == BOOT_UPDATE_OK);
    }
    f = end_frame();
    assert(boot_lifecycle_receive(&life, &f, chunks + 1u) == BOOT_UPDATE_BAD_CRC);
    assert(boot_lifecycle_activate(&life) == BOOT_LIFECYCLE_BAD_STATE);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);

    seed();
    f = start_frame(BOOT_REGION_STAGING_BYTES + 4, new_crc);
    assert(boot_lifecycle_receive(&life, &f, 0) == BOOT_UPDATE_BAD_HEADER);
    f = start_frame(image_bytes, new_crc);
    assert(boot_lifecycle_receive(&life, &f, 0) == BOOT_UPDATE_OK);
    f = chunk_frame(new_image, 1);
    assert(boot_lifecycle_receive(&life, &f, 1) == BOOT_UPDATE_BAD_SEQUENCE);
    f = chunk_frame(new_image, 0);
    assert(boot_lifecycle_receive(&life, &f, 2) == BOOT_UPDATE_OK);
    f = end_frame();
    assert(boot_lifecycle_receive(&life, &f, 3) == BOOT_UPDATE_BAD_SEQUENCE);
    f = start_frame(image_bytes, new_crc);
    assert(boot_lifecycle_receive(&life, &f, 4) == BOOT_UPDATE_OK);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
    assert_boot_protected();
}

static void boundaries(void)
{
    seed();
    const uint32_t starts[13] = {
        0x08000000u, 0x08004000u, 0x08008000u, 0x0800C000u,
        0x08010000u, 0x08020000u, 0x08040000u, 0x08060000u,
        0x08080000u, 0x080A0000u, 0x080C0000u, 0x080E0000u,
        0x08100000u
    };
    for (uint8_t sector = 0; sector < 12; ++sector) {
        uint32_t base, bytes;
        assert(boot_flash_sector_bounds(sector, &base, &bytes));
        assert(base == starts[sector] &&
               bytes == starts[sector + 1] - starts[sector]);
    }
    BootFlashBackend flash = boot_test_flash_backend(&model);
    uint8_t word[4] = {1, 2, 3, 4};
    assert(!boot_flash_erase_image(&flash, BOOT_REGION_BOOT, 4));
    assert(!boot_flash_erase_image(&flash, BOOT_REGION_METADATA, 4));
    assert(!boot_flash_program_image(&flash, BOOT_REGION_BOOT, 0, word, 4));
    assert(!boot_flash_program_image(&flash, BOOT_REGION_STAGING,
                                      BOOT_REGION_STAGING_BYTES, word, 4));
    assert(!boot_flash_program_image(&flash, BOOT_REGION_ACTIVE,
                                      BOOT_REGION_ACTIVE_BYTES - 4, word, 8));
    assert(!boot_flash_program_metadata_word(&flash,
                                              BOOT_REGION_METADATA_BYTES, 0));
    assert(!boot_flash_range_inside(BOOT_REGION_ACTIVE, UINT32_MAX - 3, 8));
    assert_boot_protected();
}

static unsigned staging_cut_samples(void)
{
    unsigned passed = 0;
    BootWireFrame start = start_frame(image_bytes, new_crc);
    BootWireFrame first = chunk_frame(new_image, 0);
    seed();
    model.fail_at = model.operations + 1; /* interrupted Staging erase */
    assert(boot_lifecycle_receive(&life, &start, 0) ==
           BOOT_UPDATE_SINK_ERROR);
    model.fail_at = 0;
    boot_lifecycle_init(&life, boot_test_flash_backend(&model));
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert_boot_protected();
    ++passed;

    seed();
    assert(boot_lifecycle_receive(&life, &start, 0) == BOOT_UPDATE_OK);
    boot_lifecycle_init(&life, boot_test_flash_backend(&model));
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    ++passed;

    seed();
    assert(boot_lifecycle_receive(&life, &start, 0) == BOOT_UPDATE_OK);
    model.fail_at = model.operations + 2; /* torn first CHUNK word */
    assert(boot_lifecycle_receive(&life, &first, 1) ==
           BOOT_UPDATE_SINK_ERROR);
    model.fail_at = 0;
    boot_lifecycle_init(&life, boot_test_flash_backend(&model));
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    ++passed;

    seed();
    assert(boot_lifecycle_receive(&life, &start, 0) == BOOT_UPDATE_OK);
    for (uint16_t i = 0; i < end_frame().sequence; ++i) {
        BootWireFrame f = chunk_frame(new_image, i);
        assert(boot_lifecycle_receive(&life, &f, i + 1u) == BOOT_UPDATE_OK);
    }
    boot_lifecycle_init(&life, boot_test_flash_backend(&model));
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    ++passed;

    seed();
    transfer(); /* END validated, but activation has not begun */
    boot_lifecycle_init(&life, boot_test_flash_backend(&model));
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
    assert_boot_protected();
    ++passed;
    return passed;
}

static unsigned power_cut_samples(void)
{
    unsigned passed = 0;
    const uint64_t words = image_bytes / 4u;
    const uint64_t cuts[] = {
        1, 2, 3, 3 + words / 2, 2 + words,
        3 + words, 2 + words + 8, 2 + words + 16,
        2 + words + 17, 2 + words + 18,
        2 + words + 19, 2 + words + 18 + words / 2,
        2 + words + 18 + words,
        2 + words + 18 + words + 1,
        2 + words + 18 + words + 15
    };
    for (unsigned i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
        seed();
        transfer();
        uint64_t before = model.operations;
        model.fail_at = before + cuts[i];
        assert(boot_lifecycle_activate(&life) != BOOT_LIFECYCLE_OK);
        model.fail_at = 0; /* restart restores the power supply */
        boot_lifecycle_init(&life, boot_test_flash_backend(&model));
        assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
        assert(boot_lifecycle_validate(&life, BOOT_REGION_ACTIVE,
                                        life.latest.active_version,
                                        life.latest.active_bytes,
                                        life.latest.active_crc));
        assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                      old_image, image_bytes) == 0);
        assert_boot_protected();
        ++passed;
    }
    return passed;
}

static void rollback_cut(void)
{
    seed();
    transfer();
    assert(boot_lifecycle_activate(&life) == BOOT_LIFECYCLE_OK);
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    model.fail_at = model.operations + 2; /* interrupted restore copy */
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_RECOVERY);
    model.fail_at = 0;
    boot_lifecycle_init(&life, boot_test_flash_backend(&model));
    assert(boot_lifecycle_boot(&life) == BOOT_ACTION_BOOT_ACTIVE);
    assert(memcmp(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
                  old_image, image_bytes) == 0);
    assert_boot_protected();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 8 && size <= BOOT_REGION_ACTIVE_BYTES && !(size & 3));
    image_bytes = (uint32_t)size;
    assert(fseek(file, 0, SEEK_SET) == 0);
    old_image = malloc(image_bytes);
    new_image = malloc(image_bytes);
    assert(old_image && new_image);
    assert(fread(new_image, 1, image_bytes, file) == image_bytes);
    fclose(file);
    memcpy(old_image, new_image, image_bytes);
    old_image[12] ^= 0x5Au;
    old_crc = boot_crc32(old_image, image_bytes);
    new_crc = boot_crc32(new_image, image_bytes);
    assert(old_crc != new_crc);

    boundaries();
    transfer_failures();
    normal_and_rollback();
    unsigned staging_cuts = staging_cut_samples();
    unsigned cuts = power_cut_samples();
    rollback_cut();
    printf("PASS lifecycle: image=%u, chunks=%u, staging_cuts=%u, "
           "activation_cuts=%u, rollback_cut=1, exact_backup_restore=1\n",
           image_bytes,
           (image_bytes + BOOT_UPDATE_CHUNK_BYTES - 1) /
             BOOT_UPDATE_CHUNK_BYTES, staging_cuts, cuts);
    free(old_image);
    free(new_image);
    return 0;
}
