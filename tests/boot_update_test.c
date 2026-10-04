#include "boot_update.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t image[1024];
    uint32_t expected_bytes;
    unsigned begins, writes, finishes;
} MemorySink;

static bool begin(void *context, uint32_t bytes)
{
    MemorySink *sink = context;
    if (bytes > sizeof(sink->image)) return false;
    memset(sink->image, 0xFF, sizeof(sink->image));
    sink->expected_bytes = bytes;
    sink->begins++;
    return true;
}

static bool write_chunk(void *context, uint32_t offset,
                        const uint8_t *bytes, uint16_t length)
{
    MemorySink *sink = context;
    if (offset > sink->expected_bytes ||
        length > sink->expected_bytes - offset) return false;
    memcpy(sink->image + offset, bytes, length);
    sink->writes++;
    return true;
}

static bool read_chunk(void *context, uint32_t offset,
                       uint8_t *bytes, uint16_t length)
{
    MemorySink *sink = context;
    if (offset > sink->expected_bytes ||
        length > sink->expected_bytes - offset) return false;
    memcpy(bytes, sink->image + offset, length);
    return true;
}

static bool finish(void *context)
{
    MemorySink *sink = context;
    sink->finishes++;
    return true;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static BootWireFrame start_frame(const uint8_t *image, uint32_t bytes,
                                 uint32_t forced_image_crc)
{
    BootImageHeader header = {BOOT_IMAGE_MAGIC, BOOT_IMAGE_FORMAT, 9,
                              bytes, forced_image_crc, 0};
    header.header_crc32 = boot_header_crc32(&header);
    BootWireFrame frame = {0};
    frame.type = BOOT_FRAME_START;
    frame.length = 24;
    put32(frame.payload, header.magic);
    put32(frame.payload + 4, header.format);
    put32(frame.payload + 8, header.firmware_version);
    put32(frame.payload + 12, header.image_bytes);
    put32(frame.payload + 16, header.image_crc32);
    put32(frame.payload + 20, header.header_crc32);
    (void)image;
    return frame;
}

static BootWireFrame chunk_frame(const uint8_t *image, uint16_t sequence,
                                  uint16_t length)
{
    BootWireFrame frame = {0};
    frame.type = BOOT_FRAME_CHUNK;
    frame.sequence = sequence;
    frame.length = length;
    memcpy(frame.payload, image + (uint32_t)sequence * 256u, length);
    return frame;
}

static BootWireFrame end_frame(uint16_t sequence)
{
    BootWireFrame frame = {0};
    frame.type = BOOT_FRAME_END;
    frame.sequence = sequence;
    return frame;
}

static BootUpdateResult exchange(BootUpdateSession *session,
                                  BootWireParser *parser,
                                  const BootWireFrame *request,
                                  uint32_t now_ms)
{
    uint8_t encoded[BOOT_WIRE_MAX_BYTES], reply[BOOT_WIRE_MAX_BYTES];
    size_t bytes = boot_wire_encode(request, encoded, sizeof(encoded));
    assert(bytes == 11u + request->length);
    BootWireFrame parsed = {0};
    unsigned received = 0;
    for (size_t i = 0; i < bytes; ++i) {
        BootParseResult result = boot_wire_feed(parser, encoded[i], &parsed);
        if (result == BOOT_PARSE_FRAME) received++;
        else assert(result == BOOT_PARSE_NONE);
    }
    assert(received == 1 && parsed.type == request->type &&
           parsed.sequence == request->sequence);
    BootUpdateResult result = boot_update_handle(session, &parsed, now_ms);
    size_t reply_size = boot_update_response(request->type, request->sequence,
                                              result,
                                              reply, sizeof(reply));
    BootWireParser response_parser = {0};
    BootWireFrame response = {0};
    for (size_t i = 0; i < reply_size; ++i)
        if (boot_wire_feed(&response_parser, reply[i], &response) ==
            BOOT_PARSE_FRAME) {
            assert(response.sequence == request->sequence &&
                   response.length == 2 && response.payload[0] == result &&
                   response.payload[1] == request->type &&
                   response.type == (result == BOOT_UPDATE_OK ?
                                    BOOT_FRAME_ACK : BOOT_FRAME_NACK));
        }
    return result;
}

int main(void)
{
    uint8_t image[600];
    memset(image, 0x5A, sizeof(image));
    put32(image, 0x2001FFF8u);
    put32(image + 4, BOOT_APP_BASE + 0x21u);
    MemorySink sink = {0};
    BootUpdateSink adapter = {begin, write_chunk, read_chunk, finish, &sink};
    BootUpdateSession session;
    BootWireParser parser = {0};
    boot_update_init(&session, adapter);
    BootWireFrame start = start_frame(image, sizeof(image),
                                      boot_crc32(image, sizeof(image)));
    BootWireFrame first = chunk_frame(image, 0, 256);
    BootWireFrame second = chunk_frame(image, 1, 256);
    BootWireFrame third = chunk_frame(image, 2, 88);
    BootWireFrame early_end = end_frame(2);
    BootWireFrame end = end_frame(3);
    assert(exchange(&session, &parser, &start, 0) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &first, 10) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &first, 20) == BOOT_UPDATE_OK);
    assert(sink.writes == 1 && session.bytes_received == 256);
    first.payload[15] ^= 1u;
    assert(exchange(&session, &parser, &first, 25) == BOOT_UPDATE_BAD_CHUNK);
    first.payload[15] ^= 1u;
    assert(exchange(&session, &parser, &third, 30) == BOOT_UPDATE_BAD_SEQUENCE);
    assert(exchange(&session, &parser, &second, 40) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &early_end, 45) == BOOT_UPDATE_INCOMPLETE);
    assert(exchange(&session, &parser, &third, 50) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &end, 60) == BOOT_UPDATE_OK);
    assert(session.complete && !session.active && sink.finishes == 1 &&
           memcmp(image, sink.image, sizeof(image)) == 0);
    assert(exchange(&session, &parser, &end, 70) == BOOT_UPDATE_OK);
    assert(sink.finishes == 1);

    /* Frame parser rejects a bad wire CRC and an oversized length. */
    uint8_t encoded[BOOT_WIRE_MAX_BYTES];
    size_t length = boot_wire_encode(&first, encoded, sizeof(encoded));
    encoded[length - 1] ^= 1u;
    BootWireFrame decoded = {0};
    BootParseResult parse = BOOT_PARSE_NONE;
    for (size_t i = 0; i < length; ++i)
        parse = boot_wire_feed(&parser, encoded[i], &decoded);
    assert(parse == BOOT_PARSE_BAD_FRAME);
    const uint8_t oversized[7] = {0xA5, 1, 2, 0, 0, 1, 1};
    for (size_t i = 0; i < sizeof(oversized); ++i)
        parse = boot_wire_feed(&parser, oversized[i], &decoded);
    assert(parse == BOOT_PARSE_BAD_FRAME);

    boot_update_init(&session, adapter);
    assert(exchange(&session, &parser, &start, 100) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &first, 2101) == BOOT_UPDATE_TIMEOUT);
    assert(!session.active);
    assert(exchange(&session, &parser, &first, 2102) == BOOT_UPDATE_BAD_STATE);

    BootWireFrame wrong_crc = start_frame(image, sizeof(image), 0);
    assert(exchange(&session, &parser, &wrong_crc, 2200) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &first, 2210) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &second, 2220) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &third, 2230) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &end, 2240) == BOOT_UPDATE_BAD_CRC);

    BootWireFrame oversized_start = start_frame(image,
        BOOT_APP_SLOT_BYTES + 4u, 0);
    assert(exchange(&session, &parser, &oversized_start, 2300) ==
           BOOT_UPDATE_BAD_HEADER);
    BootWireFrame bad_header = start;
    bad_header.payload[20] ^= 1u;
    assert(exchange(&session, &parser, &bad_header, 2400) ==
           BOOT_UPDATE_BAD_HEADER);

    /* Repeated START discards a partial transfer and begins at chunk zero. */
    assert(exchange(&session, &parser, &start, 2500) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &first, 2510) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &start, 2520) == BOOT_UPDATE_OK);
    assert(session.next_chunk == 0 && session.bytes_received == 0);
    assert(exchange(&session, &parser, &first, 2530) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &second, 2540) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &third, 2550) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &end, 2560) == BOOT_UPDATE_OK);

    uint8_t wrong_vector[600];
    memcpy(wrong_vector, image, sizeof(image));
    put32(wrong_vector + 4, BOOT_APP_BASE + 0x20u);
    BootWireFrame vector_start = start_frame(wrong_vector, sizeof(wrong_vector),
                                             boot_crc32(wrong_vector,
                                                        sizeof(wrong_vector)));
    BootWireFrame v0 = chunk_frame(wrong_vector, 0, 256);
    BootWireFrame v1 = chunk_frame(wrong_vector, 1, 256);
    BootWireFrame v2 = chunk_frame(wrong_vector, 2, 88);
    assert(exchange(&session, &parser, &vector_start, 2600) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &v0, 2610) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &v1, 2620) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &v2, 2630) == BOOT_UPDATE_OK);
    assert(exchange(&session, &parser, &end, 2640) == BOOT_UPDATE_BAD_VECTOR);
    puts("PASS UART update framing, duplicate/missing chunks, restart, timeout, CRC and vector guards");
    return 0;
}
