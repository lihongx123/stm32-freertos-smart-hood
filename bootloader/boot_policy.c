#include "boot_policy.h"

BootAction boot_policy_decide(const BootRecord *record, bool active_valid,
                              bool backup_valid, uint32_t max_attempts)
{
    if (!record || !boot_record_valid(record) || !max_attempts)
        return BOOT_ACTION_RECOVERY;
    switch (record->state) {
    case BOOT_STATE_CONFIRMED:
        if (active_valid) return BOOT_ACTION_BOOT_ACTIVE;
        return backup_valid ? BOOT_ACTION_RESTORE_BACKUP : BOOT_ACTION_RECOVERY;
    case BOOT_STATE_BACKUP_READY:
        if (!backup_valid) return BOOT_ACTION_RECOVERY;
        return active_valid ? BOOT_ACTION_BOOT_ACTIVE :
                              BOOT_ACTION_RESTORE_BACKUP;
    case BOOT_STATE_PENDING:
        if (!backup_valid) return BOOT_ACTION_RECOVERY;
        if (!active_valid || record->attempts >= max_attempts)
            return BOOT_ACTION_RESTORE_BACKUP;
        return BOOT_ACTION_RECORD_ATTEMPT_THEN_BOOT;
    default:
        return BOOT_ACTION_RECOVERY;
    }
}
