#include "boot_policy.h"
#include <assert.h>
#include <stdio.h>

static BootRecord record_for(BootState state)
{
    BootRecord record = {0};
    record.magic = BOOT_RECORD_MAGIC;
    record.format = BOOT_RECORD_FORMAT;
    record.sequence = 1;
    record.state = state;
    record.active_version = 8;
    record.active_bytes = 1024;
    record.active_crc = 0x12345678u;
    record.backup_version = 7;
    record.backup_bytes = 1024;
    record.backup_crc = 0x87654321u;
    record.stage_version = 8;
    record.stage_bytes = 1024;
    record.stage_crc = record.active_crc;
    record.commit = BOOT_RECORD_COMMIT;
    record.metadata_crc = boot_record_crc(&record);
    assert(boot_record_valid(&record));
    return record;
}

int main(void)
{
    BootRecord confirmed = record_for(BOOT_STATE_CONFIRMED);
    assert(boot_policy_decide(&confirmed, true, false, 2) ==
           BOOT_ACTION_BOOT_ACTIVE);
    assert(boot_policy_decide(&confirmed, false, true, 2) ==
           BOOT_ACTION_RESTORE_BACKUP);
    assert(boot_policy_decide(&confirmed, false, false, 2) ==
           BOOT_ACTION_RECOVERY);
    BootRecord ready = record_for(BOOT_STATE_BACKUP_READY);
    assert(boot_policy_decide(&ready, true, true, 2) ==
           BOOT_ACTION_BOOT_ACTIVE);
    assert(boot_policy_decide(&ready, false, true, 2) ==
           BOOT_ACTION_RESTORE_BACKUP);
    assert(boot_policy_decide(&ready, true, false, 2) ==
           BOOT_ACTION_RECOVERY);
    BootRecord pending = record_for(BOOT_STATE_PENDING);
    assert(boot_policy_decide(&pending, true, true, 2) ==
           BOOT_ACTION_RECORD_ATTEMPT_THEN_BOOT);
    assert(boot_policy_decide(&pending, false, true, 2) ==
           BOOT_ACTION_RESTORE_BACKUP);
    pending.attempts = 2;
    pending.metadata_crc = boot_record_crc(&pending);
    assert(boot_policy_decide(&pending, true, true, 2) ==
           BOOT_ACTION_RESTORE_BACKUP);
    assert(boot_policy_decide(&pending, true, false, 2) ==
           BOOT_ACTION_RECOVERY);
    assert(boot_policy_decide(&pending, true, true, 0) ==
           BOOT_ACTION_RECOVERY);
    pending.metadata_crc ^= 1u;
    assert(boot_policy_decide(&pending, true, true, 2) ==
           BOOT_ACTION_RECOVERY);
    puts("PASS boot decisions: confirmed, backup-ready, pending attempts and recovery");
    return 0;
}
