#include "boot_journal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t bytes[BOOT_RECORD_BYTES * 4];
    unsigned calls;
    int fail_at;
} Flash;

static bool program_word(void *context, size_t offset, uint32_t value)
{
    Flash *flash = context;
    if ((int)flash->calls++ == flash->fail_at) return false;
    if (offset + 4 > sizeof(flash->bytes) || (offset & 3u)) return false;
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t byte = (uint8_t)(value >> (8u * i));
        if ((flash->bytes[offset + i] & byte) != byte) return false;
        flash->bytes[offset + i] &= byte;
    }
    return true;
}

static BootRecord confirmed(void)
{
    BootRecord record = {0};
    record.sequence = 1;
    record.state = BOOT_STATE_CONFIRMED;
    record.active_version = 7;
    record.active_bytes = 1024;
    record.active_crc = 0x12345678u;
    return record;
}

static BootRecord backup_ready(void)
{
    BootRecord record = confirmed();
    record.sequence = 2;
    record.state = BOOT_STATE_BACKUP_READY;
    record.backup_version = 7;
    record.backup_bytes = 1024;
    record.backup_crc = record.active_crc;
    record.stage_version = 8;
    record.stage_bytes = 2048;
    record.stage_crc = 0x87654321u;
    return record;
}

int main(void)
{
    Flash initial;
    memset(&initial, 0xFF, sizeof(initial));
    initial.calls = 0;
    initial.fail_at = -1;
    BootRecord latest;
    size_t next = 999;
    assert(boot_journal_scan(NULL, sizeof(initial.bytes), &latest, &next) ==
           BOOT_JOURNAL_BAD_ARGUMENT);
    assert(boot_journal_scan(initial.bytes, sizeof(initial.bytes) - 1,
                             &latest, &next) == BOOT_JOURNAL_BAD_ARGUMENT);
    assert(boot_journal_scan(initial.bytes, sizeof(initial.bytes),
                             &latest, &next) == BOOT_JOURNAL_EMPTY);
    assert(next == 0);
    BootRecord first = confirmed();
    assert(boot_journal_append(initial.bytes, sizeof(initial.bytes),
                               &first, program_word, &initial) == BOOT_JOURNAL_OK);
    assert(boot_journal_scan(initial.bytes, sizeof(initial.bytes),
                             &latest, &next) == BOOT_JOURNAL_OK);
    assert(latest.sequence == 1 && latest.state == BOOT_STATE_CONFIRMED);
    assert(next == BOOT_RECORD_BYTES);
    assert(boot_journal_append(initial.bytes, sizeof(initial.bytes),
                               &first, program_word, &initial) ==
           BOOT_JOURNAL_BAD_RECORD); /* No repeated or skipped sequence. */

    BootRecord second = backup_ready();
    for (int cut = 0; cut < 16; ++cut) {
        Flash flash = initial;
        flash.calls = 0;
        flash.fail_at = cut;
        assert(boot_journal_append(flash.bytes, sizeof(flash.bytes),
                                   &second, program_word, &flash) ==
               BOOT_JOURNAL_WRITE_FAILED);
        assert(boot_journal_scan(flash.bytes, sizeof(flash.bytes),
                                 &latest, &next) == BOOT_JOURNAL_OK);
        assert(latest.sequence == 1);
        assert(next == (cut == 0 ? BOOT_RECORD_BYTES : BOOT_RECORD_BYTES * 2));
        flash.calls = 0;
        flash.fail_at = -1;
        assert(boot_journal_append(flash.bytes, sizeof(flash.bytes),
                                   &second, program_word, &flash) == BOOT_JOURNAL_OK);
        assert(boot_journal_scan(flash.bytes, sizeof(flash.bytes),
                                 &latest, &next) == BOOT_JOURNAL_OK);
        assert(latest.sequence == 2 && latest.state == BOOT_STATE_BACKUP_READY);
    }

    Flash flash = initial;
    flash.calls = 0;
    assert(boot_journal_append(flash.bytes, sizeof(flash.bytes),
                               &second, program_word, &flash) == BOOT_JOURNAL_OK);
    flash.bytes[BOOT_RECORD_BYTES + 12] ^= 1u; /* Corrupt newest state. */
    assert(boot_journal_scan(flash.bytes, sizeof(flash.bytes),
                             &latest, &next) == BOOT_JOURNAL_OK);
    assert(latest.sequence == 1 && next == BOOT_RECORD_BYTES * 2);

    Flash bad_commit = initial;
    bad_commit.calls = 0;
    assert(boot_journal_append(bad_commit.bytes, sizeof(bad_commit.bytes),
                               &second, program_word, &bad_commit) == BOOT_JOURNAL_OK);
    bad_commit.bytes[BOOT_RECORD_BYTES * 2 - 1] ^= 1u;
    assert(boot_journal_scan(bad_commit.bytes, sizeof(bad_commit.bytes),
                             &latest, &next) == BOOT_JOURNAL_OK);
    assert(latest.sequence == 1);

    BootRecord third = second;
    third.sequence = 2; /* Torn/corrupt seq 2 can be retried in a new slot. */
    third.state = BOOT_STATE_PENDING;
    third.active_version = 8;
    third.active_bytes = third.stage_bytes;
    third.active_crc = third.stage_crc;
    flash.calls = 0;
    assert(boot_journal_append(flash.bytes, sizeof(flash.bytes),
                               &third, program_word, &flash) == BOOT_JOURNAL_OK);
    assert(boot_journal_scan(flash.bytes, sizeof(flash.bytes),
                             &latest, &next) == BOOT_JOURNAL_OK);
    assert(latest.sequence == 2 && latest.state == BOOT_STATE_PENDING);
    third.sequence = 3;
    flash.calls = 0;
    assert(boot_journal_append(flash.bytes, sizeof(flash.bytes),
                               &third, program_word, &flash) == BOOT_JOURNAL_OK);
    third.sequence = 4;
    assert(boot_journal_append(flash.bytes, sizeof(flash.bytes),
                               &third, program_word, &flash) == BOOT_JOURNAL_FULL);
    puts("PASS append-only boot journal, 16 interrupted words, CRC and full-sector guard");
    return 0;
}
