#ifndef BOOT_JOURNAL_H
#define BOOT_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boot_flash_layout.h"

enum {
    BOOT_JOURNAL_BYTES = BOOT_REGION_METADATA_BYTES,
    BOOT_RECORD_BYTES = 64u,
    BOOT_RECORD_MAGIC = 0x424F4F54u,
    BOOT_RECORD_FORMAT = 1u,
    BOOT_RECORD_COMMIT = 0xC0DEC0DEu
};

typedef enum {
    BOOT_STATE_CONFIRMED = 1,
    BOOT_STATE_BACKUP_READY = 2,
    BOOT_STATE_PENDING = 3
} BootState;

/* 16 x 32-bit words; metadata_crc covers words 0..13, commit is word 15. */
typedef struct {
    uint32_t magic, format, sequence, state;
    uint32_t active_version, active_bytes, active_crc;
    uint32_t backup_version, backup_bytes, backup_crc;
    uint32_t stage_version, stage_bytes, stage_crc;
    uint32_t attempts, metadata_crc, commit;
} BootRecord;

typedef enum {
    BOOT_JOURNAL_OK = 0,
    BOOT_JOURNAL_EMPTY,
    BOOT_JOURNAL_FULL,
    BOOT_JOURNAL_BAD_ARGUMENT,
    BOOT_JOURNAL_BAD_RECORD,
    BOOT_JOURNAL_WRITE_FAILED
} BootJournalResult;

/* Callback writes one 32-bit word; Flash adapters must enforce erased 1->0. */
typedef bool (*BootWriteWord)(void *context, size_t offset, uint32_t value);

uint32_t boot_record_crc(const BootRecord *record);
bool boot_record_valid(const BootRecord *record);
BootJournalResult boot_journal_scan(const uint8_t *sector, size_t bytes,
                                    BootRecord *latest, size_t *next_offset);
BootJournalResult boot_journal_append(const uint8_t *sector, size_t bytes,
                                      const BootRecord *record,
                                      BootWriteWord write_word, void *context);

#endif
