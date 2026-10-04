#define _POSIX_C_SOURCE 200809L
#include "boot_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t staging[BOOT_APP_SLOT_BYTES];
static uint32_t staged_bytes;
static unsigned writes, finishes, dropped_acks;
static int drop_chunk_ack, stale_start_ack;

static bool sink_begin(void *context, uint32_t image_bytes)
{
    (void)context;
    if (image_bytes > sizeof(staging)) return false;
    memset(staging, 0xFF, sizeof(staging));
    staged_bytes = image_bytes;
    writes = finishes = 0;
    return true;
}

static bool sink_write(void *context, uint32_t offset,
                       const uint8_t *bytes, uint16_t length)
{
    (void)context;
    if (offset > staged_bytes || length > staged_bytes - offset) return false;
    memcpy(staging + offset, bytes, length);
    ++writes;
    return true;
}

static bool sink_read(void *context, uint32_t offset,
                      uint8_t *bytes, uint16_t length)
{
    (void)context;
    if (offset > staged_bytes || length > staged_bytes - offset) return false;
    memcpy(bytes, staging + offset, length);
    return true;
}

static bool sink_finish(void *context)
{
    (void)context;
    ++finishes;
    return true;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--drop-chunk-ack") == 0) drop_chunk_ack = 1;
        else if (strcmp(argv[i], "--stale-start-ack") == 0)
            stale_start_ack = 1;
        else return 4;
    }
    BootUpdateSink sink = {sink_begin, sink_write, sink_read, sink_finish, NULL};
    BootUpdateSession session;
    BootWireParser parser = {0};
    BootWireFrame frame;
    boot_update_init(&session, sink);
    uint32_t now_ms = 0;
    int byte;
    while ((byte = getchar()) != EOF) {
        BootParseResult parsed = boot_wire_feed(&parser, (uint8_t)byte, &frame);
        if (parsed == BOOT_PARSE_NONE) continue;
        if (parsed == BOOT_PARSE_BAD_FRAME) {
            fprintf(stderr, "malformed frame\n");
            return 2;
        }
        BootUpdateResult result = boot_update_handle(&session, &frame, ++now_ms);
        if (drop_chunk_ack && frame.type == BOOT_FRAME_CHUNK &&
            frame.sequence == 0 && !dropped_acks && result == BOOT_UPDATE_OK) {
            ++dropped_acks;
            continue;
        }
        uint8_t reply[BOOT_WIRE_MAX_BYTES];
        size_t reply_bytes = boot_update_response(frame.type, frame.sequence,
                                                  result,
                                                  reply, sizeof(reply));
        if (!reply_bytes || fwrite(reply, 1, reply_bytes, stdout) != reply_bytes ||
            fflush(stdout) != 0) return 3;
        if (stale_start_ack && frame.type == BOOT_FRAME_START &&
            fwrite(reply, 1, reply_bytes, stdout) != reply_bytes) return 3;
        if (stale_start_ack && frame.type == BOOT_FRAME_START &&
            fflush(stdout) != 0) return 3;
    }
    fprintf(stderr,
            "{\"complete\":%s,\"bytes\":%u,\"crc32\":%u,"
            "\"writes\":%u,\"finishes\":%u,\"dropped_acks\":%u}\n",
            session.complete ? "true" : "false", staged_bytes,
            boot_crc32(staging, staged_bytes), writes, finishes, dropped_acks);
    return session.complete && finishes == 1 ? 0 : 1;
}
