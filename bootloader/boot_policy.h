#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include <stdbool.h>
#include "boot_journal.h"

typedef enum {
    BOOT_ACTION_BOOT_ACTIVE = 0,
    BOOT_ACTION_RECORD_ATTEMPT_THEN_BOOT,
    BOOT_ACTION_RESTORE_BACKUP,
    BOOT_ACTION_RECOVERY
} BootAction;

/* Inputs mean full image CRC plus vector validation, not only CRC equality. */
BootAction boot_policy_decide(const BootRecord *record, bool active_valid,
                              bool backup_valid, uint32_t max_attempts);

#endif
