#ifndef BOOT_UPDATE_H
#define BOOT_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boot_image.h"

enum {
    BOOT_WIRE_SOF = 0xA5u,
    BOOT_WIRE_VERSION = 1u,
    BOOT_UPDATE_CHUNK_BYTES = 256u,
    BOOT_WIRE_MAX_BYTES = 11u + BOOT_UPDATE_CHUNK_BYTES,
    BOOT_UPDATE_TIMEOUT_MS = 2000u
};

typedef enum {
    BOOT_FRAME_START = 1,
    BOOT_FRAME_CHUNK = 2,
    BOOT_FRAME_END = 3,
    BOOT_FRAME_ACK = 0x80,
    BOOT_FRAME_NACK = 0x81,
    BOOT_FRAME_STATUS = 0x82
} BootFrameType;

typedef struct {
    uint8_t type;
    uint16_t sequence, length;
    uint8_t payload[BOOT_UPDATE_CHUNK_BYTES];
} BootWireFrame;

typedef enum {
    BOOT_PARSE_NONE = 0,
    BOOT_PARSE_FRAME,
    BOOT_PARSE_BAD_FRAME
} BootParseResult;

typedef struct {
    uint8_t bytes[BOOT_WIRE_MAX_BYTES];
    uint16_t used, expected;
} BootWireParser;

size_t boot_wire_encode(const BootWireFrame *frame, uint8_t *output,
                        size_t capacity);
BootParseResult boot_wire_feed(BootWireParser *parser, uint8_t byte,
                                BootWireFrame *frame);

/* A later MCU adapter can write staging Flash; host tests use memory. */
typedef struct {
    bool (*begin)(void *context, uint32_t image_bytes);
    bool (*write)(void *context, uint32_t offset,
                  const uint8_t *bytes, uint16_t length);
    bool (*read)(void *context, uint32_t offset,
                 uint8_t *bytes, uint16_t length);
    bool (*finish)(void *context);
    void *context;
} BootUpdateSink;

typedef enum {
    BOOT_UPDATE_OK = 0,
    BOOT_UPDATE_BAD_STATE,
    BOOT_UPDATE_BAD_HEADER,
    BOOT_UPDATE_BAD_SEQUENCE,
    BOOT_UPDATE_BAD_CHUNK,
    BOOT_UPDATE_BAD_CRC,
    BOOT_UPDATE_BAD_VECTOR,
    BOOT_UPDATE_TIMEOUT,
    BOOT_UPDATE_SINK_ERROR,
    BOOT_UPDATE_INCOMPLETE
} BootUpdateResult;

typedef struct {
    BootUpdateSink sink;
    BootImageHeader header;
    uint32_t crc_state, bytes_received, last_activity_ms;
    uint16_t next_chunk;
    uint8_t vectors[8];
    bool active, complete;
} BootUpdateSession;

void boot_update_init(BootUpdateSession *session, BootUpdateSink sink);
BootUpdateResult boot_update_handle(BootUpdateSession *session,
                                     const BootWireFrame *frame,
                                     uint32_t now_ms);
size_t boot_update_response(uint8_t request_type, uint16_t sequence,
                             BootUpdateResult result,
                             uint8_t *output, size_t capacity);

#endif
