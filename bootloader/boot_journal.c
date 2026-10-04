#include "boot_journal.h"
#include "boot_image.h"

_Static_assert(sizeof(BootRecord) == BOOT_RECORD_BYTES,
               "BootRecord must occupy one 64-byte journal slot");

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static void record_words(const BootRecord *r, uint32_t w[16])
{
    w[0] = r->magic; w[1] = r->format; w[2] = r->sequence; w[3] = r->state;
    w[4] = r->active_version; w[5] = r->active_bytes; w[6] = r->active_crc;
    w[7] = r->backup_version; w[8] = r->backup_bytes; w[9] = r->backup_crc;
    w[10] = r->stage_version; w[11] = r->stage_bytes; w[12] = r->stage_crc;
    w[13] = r->attempts; w[14] = r->metadata_crc; w[15] = r->commit;
}

static void words_record(const uint32_t w[16], BootRecord *r)
{
    r->magic = w[0]; r->format = w[1]; r->sequence = w[2]; r->state = w[3];
    r->active_version = w[4]; r->active_bytes = w[5]; r->active_crc = w[6];
    r->backup_version = w[7]; r->backup_bytes = w[8]; r->backup_crc = w[9];
    r->stage_version = w[10]; r->stage_bytes = w[11]; r->stage_crc = w[12];
    r->attempts = w[13]; r->metadata_crc = w[14]; r->commit = w[15];
}

uint32_t boot_record_crc(const BootRecord *record)
{
    if (!record) return 0;
    uint32_t words[16];
    uint8_t data[56];
    record_words(record, words);
    for (unsigned i = 0; i < 14; ++i) write_le32(&data[i * 4], words[i]);
    return boot_crc32(data, sizeof(data));
}

static bool valid_size(uint32_t bytes)
{
    return bytes >= 8u && bytes <= BOOT_APP_SLOT_BYTES && !(bytes & 3u);
}

bool boot_record_valid(const BootRecord *record)
{
    if (!record || record->magic != BOOT_RECORD_MAGIC ||
        record->format != BOOT_RECORD_FORMAT || record->sequence == 0 ||
        record->commit != BOOT_RECORD_COMMIT ||
        record->metadata_crc != boot_record_crc(record) ||
        !valid_size(record->active_bytes)) return false;
    if (record->state != BOOT_STATE_CONFIRMED &&
        record->state != BOOT_STATE_BACKUP_READY &&
        record->state != BOOT_STATE_PENDING) return false;
    if (record->backup_bytes && !valid_size(record->backup_bytes)) return false;
    if (record->stage_bytes && !valid_size(record->stage_bytes)) return false;
    if (record->state != BOOT_STATE_CONFIRMED &&
        (!valid_size(record->backup_bytes) ||
         !valid_size(record->stage_bytes))) return false;
    return true;
}

BootJournalResult boot_journal_scan(const uint8_t *sector, size_t bytes,
                                    BootRecord *latest, size_t *next_offset)
{
    if (!sector || !latest || !next_offset || bytes < BOOT_RECORD_BYTES ||
        bytes > BOOT_JOURNAL_BYTES ||
        bytes % BOOT_RECORD_BYTES) return BOOT_JOURNAL_BAD_ARGUMENT;
    *next_offset = bytes;
    bool found = false;
    uint32_t highest_sequence = 0;
    for (size_t offset = 0; offset < bytes; offset += BOOT_RECORD_BYTES) {
        bool erased = true;
        for (unsigned i = 0; i < BOOT_RECORD_BYTES; ++i)
            if (sector[offset + i] != 0xFFu) { erased = false; break; }
        if (erased) {
            if (*next_offset == bytes) *next_offset = offset;
            continue;
        }
        uint32_t words[16];
        for (unsigned i = 0; i < 16; ++i)
            words[i] = read_le32(&sector[offset + i * 4]);
        BootRecord candidate;
        words_record(words, &candidate);
        if (boot_record_valid(&candidate) &&
            (!found || candidate.sequence > highest_sequence)) {
            *latest = candidate;
            highest_sequence = candidate.sequence;
            found = true;
        }
    }
    return found ? BOOT_JOURNAL_OK : BOOT_JOURNAL_EMPTY;
}

BootJournalResult boot_journal_append(const uint8_t *sector, size_t bytes,
                                      const BootRecord *record,
                                      BootWriteWord write_word, void *context)
{
    if (!sector || !record || !write_word) return BOOT_JOURNAL_BAD_ARGUMENT;
    BootRecord latest;
    size_t offset;
    BootJournalResult scan = boot_journal_scan(sector, bytes, &latest, &offset);
    if (scan != BOOT_JOURNAL_OK && scan != BOOT_JOURNAL_EMPTY) return scan;
    if (offset == bytes) return BOOT_JOURNAL_FULL;
    BootRecord prepared = *record;
    prepared.magic = BOOT_RECORD_MAGIC;
    prepared.format = BOOT_RECORD_FORMAT;
    prepared.commit = BOOT_RECORD_COMMIT;
    prepared.metadata_crc = boot_record_crc(&prepared);
    if ((scan == BOOT_JOURNAL_OK &&
         (latest.sequence == UINT32_MAX ||
          prepared.sequence != latest.sequence + 1u)) ||
        (scan == BOOT_JOURNAL_EMPTY && prepared.sequence != 1u) ||
        !boot_record_valid(&prepared)) return BOOT_JOURNAL_BAD_RECORD;
    uint32_t words[16];
    record_words(&prepared, words);
    for (unsigned i = 0; i < 16; ++i)
        if (!write_word(context, offset + i * 4, words[i]))
            return BOOT_JOURNAL_WRITE_FAILED;
    return BOOT_JOURNAL_OK;
}
