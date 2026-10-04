#include "boot_update.h"
#include <string.h>

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

static void write32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

size_t boot_wire_encode(const BootWireFrame *frame, uint8_t *output,
                        size_t capacity)
{
    if (!frame || !output || frame->length > BOOT_UPDATE_CHUNK_BYTES ||
        capacity < (size_t)frame->length + 11u) return 0;
    output[0] = BOOT_WIRE_SOF;
    output[1] = BOOT_WIRE_VERSION;
    output[2] = frame->type;
    write16(output + 3, frame->sequence);
    write16(output + 5, frame->length);
    memcpy(output + 7, frame->payload, frame->length);
    write32(output + 7 + frame->length,
            boot_crc32(output + 1, 6u + frame->length));
    return (size_t)frame->length + 11u;
}

BootParseResult boot_wire_feed(BootWireParser *parser, uint8_t byte,
                                BootWireFrame *frame)
{
    if (!parser || !frame) return BOOT_PARSE_BAD_FRAME;
    if (parser->used == 0 && byte != BOOT_WIRE_SOF) return BOOT_PARSE_NONE;
    parser->bytes[parser->used++] = byte;
    if (parser->used == 7) {
        uint16_t length = read16(parser->bytes + 5);
        if (parser->bytes[1] != BOOT_WIRE_VERSION ||
            length > BOOT_UPDATE_CHUNK_BYTES) {
            parser->used = parser->expected = 0;
            return BOOT_PARSE_BAD_FRAME;
        }
        parser->expected = (uint16_t)(length + 11u);
    }
    if (!parser->expected || parser->used < parser->expected)
        return BOOT_PARSE_NONE;
    const uint16_t length = read16(parser->bytes + 5);
    frame->type = parser->bytes[2];
    frame->sequence = read16(parser->bytes + 3);
    frame->length = length;
    memcpy(frame->payload, parser->bytes + 7, length);
    uint32_t received_crc = read32(parser->bytes + 7 + length);
    uint32_t actual_crc = boot_crc32(parser->bytes + 1, 6u + length);
    parser->used = parser->expected = 0;
    return received_crc == actual_crc ? BOOT_PARSE_FRAME :
                                        BOOT_PARSE_BAD_FRAME;
}

void boot_update_init(BootUpdateSession *session, BootUpdateSink sink)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->sink = sink;
}

static BootImageHeader parse_header(const uint8_t *p)
{
    BootImageHeader h = {read32(p), read32(p + 4), read32(p + 8),
                         read32(p + 12), read32(p + 16), read32(p + 20)};
    return h;
}

static uint16_t expected_length(const BootUpdateSession *session,
                                 uint16_t sequence)
{
    uint32_t offset = (uint32_t)sequence * BOOT_UPDATE_CHUNK_BYTES;
    if (offset >= session->header.image_bytes) return 0;
    uint32_t remaining = session->header.image_bytes - offset;
    return remaining > BOOT_UPDATE_CHUNK_BYTES ? BOOT_UPDATE_CHUNK_BYTES :
                                                  (uint16_t)remaining;
}

static BootUpdateResult handle_chunk(BootUpdateSession *session,
                                      const BootWireFrame *frame,
                                      uint32_t now_ms)
{
    uint16_t expected = expected_length(session, frame->sequence);
    if (!expected || frame->sequence > session->next_chunk)
        return BOOT_UPDATE_BAD_SEQUENCE;
    if (frame->length != expected) return BOOT_UPDATE_BAD_CHUNK;
    uint32_t offset = (uint32_t)frame->sequence * BOOT_UPDATE_CHUNK_BYTES;
    if (frame->sequence < session->next_chunk) {
        uint8_t previous[BOOT_UPDATE_CHUNK_BYTES];
        if (!session->sink.read ||
            !session->sink.read(session->sink.context, offset, previous,
                                frame->length)) return BOOT_UPDATE_SINK_ERROR;
        if (memcmp(previous, frame->payload, frame->length) != 0)
            return BOOT_UPDATE_BAD_CHUNK;
        session->last_activity_ms = now_ms;
        return BOOT_UPDATE_OK;
    }
    if (!session->sink.write ||
        !session->sink.write(session->sink.context, offset, frame->payload,
                             frame->length)) return BOOT_UPDATE_SINK_ERROR;
    if (offset == 0) memcpy(session->vectors, frame->payload, 8);
    session->crc_state = boot_crc32_update(session->crc_state,
                                            frame->payload, frame->length);
    session->bytes_received += frame->length;
    session->next_chunk++;
    session->last_activity_ms = now_ms;
    return BOOT_UPDATE_OK;
}

BootUpdateResult boot_update_handle(BootUpdateSession *session,
                                     const BootWireFrame *frame,
                                     uint32_t now_ms)
{
    if (!session || !frame) return BOOT_UPDATE_BAD_STATE;
    if (frame->type == BOOT_FRAME_START) {
        session->active = session->complete = false;
        if (frame->sequence != 0 || frame->length != 24 ||
            !session->sink.begin || !session->sink.write ||
            !session->sink.read || !session->sink.finish)
            return BOOT_UPDATE_BAD_HEADER;
        BootImageHeader header = parse_header(frame->payload);
        if (boot_header_validate(&header) != BOOT_IMAGE_OK)
            return BOOT_UPDATE_BAD_HEADER;
        if (!session->sink.begin(session->sink.context, header.image_bytes))
            return BOOT_UPDATE_SINK_ERROR;
        session->header = header;
        session->crc_state = 0xFFFFFFFFu;
        session->bytes_received = 0;
        session->next_chunk = 0;
        session->last_activity_ms = now_ms;
        session->active = true;
        return BOOT_UPDATE_OK;
    }
    if (!session->active) {
        /* The sender may retry END when its final ACK was lost. */
        if (session->complete && frame->type == BOOT_FRAME_END &&
            frame->length == 0 && frame->sequence == session->next_chunk)
            return BOOT_UPDATE_OK;
        return BOOT_UPDATE_BAD_STATE;
    }
    if (now_ms - session->last_activity_ms > BOOT_UPDATE_TIMEOUT_MS) {
        session->active = false;
        return BOOT_UPDATE_TIMEOUT;
    }
    if (frame->type == BOOT_FRAME_CHUNK)
        return handle_chunk(session, frame, now_ms);
    if (frame->type != BOOT_FRAME_END || frame->length ||
        frame->sequence != session->next_chunk)
        return BOOT_UPDATE_BAD_SEQUENCE;
    if (session->bytes_received != session->header.image_bytes)
        return BOOT_UPDATE_INCOMPLETE;
    if ((session->crc_state ^ 0xFFFFFFFFu) != session->header.image_crc32) {
        session->active = false;
        return BOOT_UPDATE_BAD_CRC;
    }
    if (boot_vectors_validate(&session->header,
                              read32(session->vectors),
                              read32(session->vectors + 4)) != BOOT_IMAGE_OK) {
        session->active = false;
        return BOOT_UPDATE_BAD_VECTOR;
    }
    if (!session->sink.finish(session->sink.context))
        return BOOT_UPDATE_SINK_ERROR;
    session->active = false;
    session->complete = true;
    return BOOT_UPDATE_OK;
}

size_t boot_update_response(uint8_t request_type, uint16_t sequence,
                             BootUpdateResult result,
                             uint8_t *output, size_t capacity)
{
    BootWireFrame frame = {0};
    frame.type = result == BOOT_UPDATE_OK ? BOOT_FRAME_ACK : BOOT_FRAME_NACK;
    frame.sequence = sequence;
    frame.length = 2;
    frame.payload[0] = (uint8_t)result;
    frame.payload[1] = request_type;
    return boot_wire_encode(&frame, output, capacity);
}
