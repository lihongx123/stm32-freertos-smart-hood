#ifndef BOOT_LIFECYCLE_H
#define BOOT_LIFECYCLE_H

#include "boot_flash.h"
#include "boot_journal.h"
#include "boot_policy.h"
#include "boot_update.h"

enum { BOOT_PENDING_MAX_ATTEMPTS = 2u, BOOT_TRANSACTION_RESERVED_RECORDS = 8u };

typedef enum {
    BOOT_LIFECYCLE_OK = 0,
    BOOT_LIFECYCLE_BAD_STATE,
    BOOT_LIFECYCLE_BAD_IMAGE,
    BOOT_LIFECYCLE_FLASH_ERROR,
    BOOT_LIFECYCLE_METADATA_ERROR
} BootLifecycleResult;

typedef enum {
    BOOT_STEP_NONE = 0,
    BOOT_STEP_BACKUP_ERASE,
    BOOT_STEP_BACKUP_COPY,
    BOOT_STEP_BACKUP_VALIDATE,
    BOOT_STEP_BACKUP_RECORD,
    BOOT_STEP_ACTIVE_ERASE,
    BOOT_STEP_ACTIVE_COPY,
    BOOT_STEP_ACTIVE_VALIDATE,
    BOOT_STEP_PENDING_RECORD,
    BOOT_STEP_RESTORE_ERASE,
    BOOT_STEP_RESTORE_COPY,
    BOOT_STEP_RESTORE_VALIDATE,
    BOOT_STEP_RESTORE_RECORD
} BootLifecycleStep;

typedef struct {
    BootFlashBackend flash;
    BootUpdateSession transfer;
    BootImageHeader incoming;
    BootRecord latest;
    BootLifecycleStep last_step;
    bool has_record, stage_valid;
} BootLifecycle;

void boot_lifecycle_init(BootLifecycle *lifecycle, BootFlashBackend flash);
bool boot_lifecycle_reload(BootLifecycle *lifecycle);
bool boot_lifecycle_validate(const BootLifecycle *lifecycle,
                             BootRegionId region, uint32_t version,
                             uint32_t bytes, uint32_t crc);
BootUpdateResult boot_lifecycle_receive(BootLifecycle *lifecycle,
                                        const BootWireFrame *frame,
                                        uint32_t now_ms);
BootLifecycleResult boot_lifecycle_activate(BootLifecycle *lifecycle);
BootAction boot_lifecycle_boot(BootLifecycle *lifecycle);
BootLifecycleResult boot_lifecycle_confirm(BootLifecycle *lifecycle);

#endif
