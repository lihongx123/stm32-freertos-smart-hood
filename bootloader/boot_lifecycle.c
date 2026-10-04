#include "boot_lifecycle.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t *metadata(const BootLifecycle *life)
{
    return boot_flash_map(&life->flash, BOOT_REGION_METADATA_BASE,
                          BOOT_REGION_METADATA_BYTES);
}

bool boot_lifecycle_reload(BootLifecycle *life)
{
    if (!life) return false;
    const uint8_t *sector = metadata(life);
    size_t next;
    life->has_record = sector &&
        boot_journal_scan(sector, BOOT_JOURNAL_BYTES, &life->latest,
                          &next) == BOOT_JOURNAL_OK;
    return life->has_record;
}

bool boot_lifecycle_validate(const BootLifecycle *life,
                             BootRegionId region, uint32_t version,
                             uint32_t bytes, uint32_t crc)
{
    if (!life || !bytes || bytes > boot_flash_region(region).bytes)
        return false;
    const uint8_t *image = boot_flash_map(&life->flash,
                                          boot_flash_region(region).base, bytes);
    if (!image) return false;
    BootImageHeader header = {BOOT_IMAGE_MAGIC, BOOT_IMAGE_FORMAT,
                              version, bytes, crc, 0};
    header.header_crc32 = boot_header_crc32(&header);
    return boot_image_validate(&header, image, bytes) == BOOT_IMAGE_OK;
}

static bool active_valid(const BootLifecycle *life)
{
    return boot_lifecycle_validate(life, BOOT_REGION_ACTIVE,
                                   life->latest.active_version,
                                   life->latest.active_bytes,
                                   life->latest.active_crc);
}

static bool backup_valid(const BootLifecycle *life)
{
    return life->latest.backup_bytes &&
        boot_lifecycle_validate(life, BOOT_REGION_BACKUP,
                                 life->latest.backup_version,
                                 life->latest.backup_bytes,
                                 life->latest.backup_crc);
}

static bool sink_begin(void *context, uint32_t image_bytes)
{
    BootLifecycle *life = context;
    life->stage_valid = false;
    if (!boot_lifecycle_reload(life) ||
        life->latest.state != BOOT_STATE_CONFIRMED || !active_valid(life))
        return false;
    return boot_flash_erase_image(&life->flash, BOOT_REGION_STAGING,
                                  image_bytes);
}

static bool sink_write(void *context, uint32_t offset,
                       const uint8_t *bytes, uint16_t length)
{
    BootLifecycle *life = context;
    return boot_flash_program_image(&life->flash, BOOT_REGION_STAGING,
                                     offset, bytes, length);
}

static bool sink_read(void *context, uint32_t offset,
                      uint8_t *bytes, uint16_t length)
{
    BootLifecycle *life = context;
    if (!boot_flash_range_inside(BOOT_REGION_STAGING,
                                  BOOT_REGION_STAGING_BASE + offset,
                                  length)) return false;
    const uint8_t *p = boot_flash_map(&life->flash,
                                      BOOT_REGION_STAGING_BASE + offset,
                                      length);
    if (!p) return false;
    memcpy(bytes, p, length);
    return true;
}

static bool sink_finish(void *context)
{
    BootLifecycle *life = context;
    life->stage_valid = boot_lifecycle_validate(
        life, BOOT_REGION_STAGING, life->incoming.firmware_version,
        life->incoming.image_bytes, life->incoming.image_crc32);
    return life->stage_valid;
}

void boot_lifecycle_init(BootLifecycle *life, BootFlashBackend flash)
{
    if (!life) return;
    memset(life, 0, sizeof(*life));
    life->flash = flash;
    BootUpdateSink sink = {sink_begin, sink_write, sink_read, sink_finish, life};
    boot_update_init(&life->transfer, sink);
    boot_lifecycle_reload(life);
}

BootUpdateResult boot_lifecycle_receive(BootLifecycle *life,
                                        const BootWireFrame *frame,
                                        uint32_t now_ms)
{
    if (!life || !frame) return BOOT_UPDATE_BAD_STATE;
    if (frame->type == BOOT_FRAME_START && frame->length == 24) {
        const uint8_t *p = frame->payload;
        life->incoming = (BootImageHeader){le32(p), le32(p + 4),
            le32(p + 8), le32(p + 12), le32(p + 16), le32(p + 20)};
    }
    return boot_update_handle(&life->transfer, frame, now_ms);
}

static bool journal_room(const BootLifecycle *life, unsigned needed)
{
    const uint8_t *sector = metadata(life);
    if (!sector) return false;
    unsigned blank = 0;
    for (size_t offset = 0; offset < BOOT_JOURNAL_BYTES;
         offset += BOOT_RECORD_BYTES) {
        bool empty = true;
        for (unsigned i = 0; i < BOOT_RECORD_BYTES; ++i)
            if (sector[offset + i] != 0xFFu) { empty = false; break; }
        if (empty && ++blank >= needed) return true;
    }
    return false;
}

static bool write_metadata(void *context, size_t offset, uint32_t word)
{
    BootLifecycle *life = context;
    return offset <= UINT32_MAX &&
        boot_flash_program_metadata_word(&life->flash, (uint32_t)offset, word);
}

static bool append(BootLifecycle *life, BootRecord record)
{
    const uint8_t *sector = metadata(life);
    if (!sector || !life->has_record || life->latest.sequence == UINT32_MAX)
        return false;
    record.sequence = life->latest.sequence + 1u;
    if (boot_journal_append(sector, BOOT_JOURNAL_BYTES, &record,
                            write_metadata, life) != BOOT_JOURNAL_OK)
        return false;
    return boot_lifecycle_reload(life);
}

BootLifecycleResult boot_lifecycle_activate(BootLifecycle *life)
{
    if (!life || !life->transfer.complete || !life->stage_valid ||
        !boot_lifecycle_reload(life) ||
        life->latest.state != BOOT_STATE_CONFIRMED)
        return BOOT_LIFECYCLE_BAD_STATE;
    if (!active_valid(life) || !boot_lifecycle_validate(life,
            BOOT_REGION_STAGING, life->incoming.firmware_version,
            life->incoming.image_bytes, life->incoming.image_crc32))
        return BOOT_LIFECYCLE_BAD_IMAGE;
    if (!journal_room(life, BOOT_TRANSACTION_RESERVED_RECORDS))
        return BOOT_LIFECYCLE_METADATA_ERROR;

    BootRecord old = life->latest;
    life->last_step = BOOT_STEP_BACKUP_ERASE;
    if (!boot_flash_erase_image(&life->flash, BOOT_REGION_BACKUP,
                                BOOT_REGION_BACKUP_BYTES))
        return BOOT_LIFECYCLE_FLASH_ERROR;
    life->last_step = BOOT_STEP_BACKUP_COPY;
    if (!boot_flash_copy_image(&life->flash, BOOT_REGION_ACTIVE,
                                BOOT_REGION_BACKUP, old.active_bytes))
        return BOOT_LIFECYCLE_FLASH_ERROR;
    life->last_step = BOOT_STEP_BACKUP_VALIDATE;
    if (!boot_lifecycle_validate(life, BOOT_REGION_BACKUP,
                                  old.active_version, old.active_bytes,
                                  old.active_crc)) return BOOT_LIFECYCLE_FLASH_ERROR;

    BootRecord transition = old;
    transition.state = BOOT_STATE_BACKUP_READY;
    transition.backup_version = old.active_version;
    transition.backup_bytes = old.active_bytes;
    transition.backup_crc = old.active_crc;
    transition.stage_version = life->incoming.firmware_version;
    transition.stage_bytes = life->incoming.image_bytes;
    transition.stage_crc = life->incoming.image_crc32;
    transition.attempts = 0;
    life->last_step = BOOT_STEP_BACKUP_RECORD;
    if (!append(life, transition)) return BOOT_LIFECYCLE_METADATA_ERROR;

    life->last_step = BOOT_STEP_ACTIVE_ERASE;
    uint32_t erase_bytes = old.active_bytes > life->incoming.image_bytes ?
        old.active_bytes : life->incoming.image_bytes;
    if (!boot_flash_erase_image(&life->flash, BOOT_REGION_ACTIVE,
                                erase_bytes))
        return BOOT_LIFECYCLE_FLASH_ERROR;
    life->last_step = BOOT_STEP_ACTIVE_COPY;
    if (!boot_flash_copy_image(&life->flash, BOOT_REGION_STAGING,
                                BOOT_REGION_ACTIVE,
                                life->incoming.image_bytes))
        return BOOT_LIFECYCLE_FLASH_ERROR;
    life->last_step = BOOT_STEP_ACTIVE_VALIDATE;
    if (!boot_lifecycle_validate(life, BOOT_REGION_ACTIVE,
                                  life->incoming.firmware_version,
                                  life->incoming.image_bytes,
                                  life->incoming.image_crc32))
        return BOOT_LIFECYCLE_FLASH_ERROR;

    transition.state = BOOT_STATE_PENDING;
    transition.active_version = life->incoming.firmware_version;
    transition.active_bytes = life->incoming.image_bytes;
    transition.active_crc = life->incoming.image_crc32;
    life->last_step = BOOT_STEP_PENDING_RECORD;
    if (!append(life, transition)) return BOOT_LIFECYCLE_METADATA_ERROR;
    life->stage_valid = false;
    return BOOT_LIFECYCLE_OK;
}

static bool restore_backup(BootLifecycle *life)
{
    if (!backup_valid(life)) return false;
    BootRecord record = life->latest;
    life->last_step = BOOT_STEP_RESTORE_ERASE;
    uint32_t erase_bytes = record.active_bytes > record.backup_bytes ?
        record.active_bytes : record.backup_bytes;
    if (!boot_flash_erase_image(&life->flash, BOOT_REGION_ACTIVE,
                                erase_bytes)) return false;
    life->last_step = BOOT_STEP_RESTORE_COPY;
    if (!boot_flash_copy_image(&life->flash, BOOT_REGION_BACKUP,
                                BOOT_REGION_ACTIVE, record.backup_bytes))
        return false;
    life->last_step = BOOT_STEP_RESTORE_VALIDATE;
    if (!boot_lifecycle_validate(life, BOOT_REGION_ACTIVE,
                                  record.backup_version, record.backup_bytes,
                                  record.backup_crc)) return false;
    record.state = BOOT_STATE_CONFIRMED;
    record.active_version = record.backup_version;
    record.active_bytes = record.backup_bytes;
    record.active_crc = record.backup_crc;
    record.attempts = 0;
    life->last_step = BOOT_STEP_RESTORE_RECORD;
    return append(life, record);
}

BootAction boot_lifecycle_boot(BootLifecycle *life)
{
    if (!life || !boot_lifecycle_reload(life)) return BOOT_ACTION_RECOVERY;
    bool active = active_valid(life), backup = backup_valid(life);
    BootAction action = boot_policy_decide(&life->latest, active, backup,
                                            BOOT_PENDING_MAX_ATTEMPTS);
    if (action == BOOT_ACTION_RECORD_ATTEMPT_THEN_BOOT) {
        if (!journal_room(life, 2)) return BOOT_ACTION_RECOVERY;
        BootRecord next = life->latest;
        next.attempts++;
        return append(life, next) ? BOOT_ACTION_BOOT_ACTIVE :
                                    BOOT_ACTION_RECOVERY;
    }
    if (action == BOOT_ACTION_RESTORE_BACKUP)
        return journal_room(life, 1) && restore_backup(life) ?
            BOOT_ACTION_BOOT_ACTIVE : BOOT_ACTION_RECOVERY;
    if (action == BOOT_ACTION_BOOT_ACTIVE &&
        life->latest.state == BOOT_STATE_BACKUP_READY) {
        BootRecord next = life->latest;
        next.state = BOOT_STATE_CONFIRMED;
        return journal_room(life, 1) && append(life, next) ?
            BOOT_ACTION_BOOT_ACTIVE : BOOT_ACTION_RECOVERY;
    }
    return action;
}

BootLifecycleResult boot_lifecycle_confirm(BootLifecycle *life)
{
    if (!life || !boot_lifecycle_reload(life) ||
        life->latest.state != BOOT_STATE_PENDING)
        return BOOT_LIFECYCLE_BAD_STATE;
    if (!active_valid(life)) return BOOT_LIFECYCLE_BAD_IMAGE;
    if (!journal_room(life, 1)) return BOOT_LIFECYCLE_METADATA_ERROR;
    BootRecord next = life->latest;
    next.state = BOOT_STATE_CONFIRMED;
    next.attempts = 0;
    return append(life, next) ? BOOT_LIFECYCLE_OK :
                                BOOT_LIFECYCLE_METADATA_ERROR;
}
