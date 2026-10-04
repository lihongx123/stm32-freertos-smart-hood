#include "boot_lifecycle.h"
#include "boot_test_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BootTestFlash model;
static BootLifecycle lifecycle;
static uint8_t current[BOOT_REGION_ACTIVE_BYTES];
static uint32_t current_bytes, current_crc;

static bool seed_word(void *context, size_t offset, uint32_t word)
{
    BootFlashBackend *backend = context;
    return boot_flash_program_metadata_word(backend, (uint32_t)offset, word);
}

static bool initialize(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) return false;
    long size = ftell(file);
    if (size < 8 || size > BOOT_REGION_ACTIVE_BYTES || (size & 3) ||
        fseek(file, 0, SEEK_SET) != 0) return false;
    current_bytes = (uint32_t)size;
    if (fread(current, 1, current_bytes, file) != current_bytes) return false;
    fclose(file);
    current[12] ^= 0x5Au;
    current_crc = boot_crc32(current, current_bytes);
    boot_test_flash_init(&model);
    memcpy(model.bytes + BOOT_REGION_ACTIVE_BASE - BOOT_FLASH_BASE,
           current, current_bytes);
    BootFlashBackend backend = boot_test_flash_backend(&model);
    BootRecord seed = {0};
    seed.sequence = 1;
    seed.state = BOOT_STATE_CONFIRMED;
    seed.active_version = 1;
    seed.active_bytes = current_bytes;
    seed.active_crc = current_crc;
    if (boot_journal_append(model.bytes + BOOT_REGION_METADATA_BASE -
                            BOOT_FLASH_BASE, BOOT_JOURNAL_BYTES, &seed,
                            seed_word, &backend) != BOOT_JOURNAL_OK) return false;
    boot_lifecycle_init(&lifecycle, backend);
    return lifecycle.has_record;
}

static bool send_frame(const BootWireFrame *frame)
{
    uint8_t bytes[BOOT_WIRE_MAX_BYTES];
    size_t count = boot_wire_encode(frame, bytes, sizeof(bytes));
    return count && fwrite(bytes, 1, count, stdout) == count &&
           fflush(stdout) == 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3 || !initialize(argv[1])) return 2;
    bool drop_end_ack = argc == 3 && strcmp(argv[2], "--drop-end-ack") == 0;
    BootWireParser parser = {0};
    BootWireFrame frame;
    uint32_t now_ms = 0;
    int byte;
    while ((byte = getchar()) != EOF) {
        BootParseResult parsed = boot_wire_feed(&parser, (uint8_t)byte, &frame);
        if (parsed == BOOT_PARSE_NONE) continue;
        if (parsed != BOOT_PARSE_FRAME) return 3;
        BootUpdateResult result = boot_lifecycle_receive(&lifecycle, &frame,
                                                          ++now_ms);
        BootWireFrame reply = {0};
        reply.type = result == BOOT_UPDATE_OK ? BOOT_FRAME_ACK : BOOT_FRAME_NACK;
        reply.sequence = frame.sequence;
        reply.length = 2;
        reply.payload[0] = (uint8_t)result;
        reply.payload[1] = frame.type;
        if (!(drop_end_ack && frame.type == BOOT_FRAME_END) &&
            !send_frame(&reply)) return 4;
        if (frame.type != BOOT_FRAME_END || result != BOOT_UPDATE_OK) continue;
        BootLifecycleResult activated = boot_lifecycle_activate(&lifecycle);
        BootWireFrame status = {0};
        status.type = BOOT_FRAME_STATUS;
        status.sequence = frame.sequence;
        status.length = 1;
        status.payload[0] = (uint8_t)activated;
        if (!send_frame(&status)) return 5;
    }
    bool pending = lifecycle.latest.state == BOOT_STATE_PENDING;
    bool backup_exact = memcmp(model.bytes + BOOT_REGION_BACKUP_BASE -
                               BOOT_FLASH_BASE, current, current_bytes) == 0;
    bool active_valid = boot_lifecycle_validate(&lifecycle,
        BOOT_REGION_ACTIVE, lifecycle.latest.active_version,
        lifecycle.latest.active_bytes, lifecycle.latest.active_crc);
    fprintf(stderr,
            "{\"pending\":%s,\"backup_exact\":%s,"
            "\"active_valid\":%s,\"bytes\":%u,\"crc32\":%u}\n",
            pending ? "true" : "false", backup_exact ? "true" : "false",
            active_valid ? "true" : "false", lifecycle.latest.active_bytes,
            lifecycle.latest.active_crc);
    return pending && backup_exact && active_valid ? 0 : 1;
}
