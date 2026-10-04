"""PC sender for the F407 bootloader UART update protocol.

The STM32 backend builds, but physical Flash updating is not validated.
Local tests exercise the full image lifecycle with a deterministic model.
"""
import argparse
import pathlib
import struct
import time
import zlib

SOF = 0xA5
VERSION = 1
START, CHUNK, END, ACK, NACK, STATUS = 1, 2, 3, 0x80, 0x81, 0x82
CHUNK_BYTES = 256
MAX_IMAGE_BYTES = 256 * 1024
IMAGE_MAGIC = 0x484F4F44
APP_BASE = 0x08020000


def encode_frame(kind, sequence, payload=b''):
    if not 0 <= sequence <= 0xFFFF or len(payload) > CHUNK_BYTES:
        raise ValueError('frame bounds exceeded')
    body = struct.pack('<BBHH', VERSION, kind, sequence, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack('<I', zlib.crc32(body))


def decode_frame(data):
    if len(data) < 11 or data[0] != SOF:
        raise ValueError('bad frame prefix')
    version, kind, sequence, length = struct.unpack_from('<BBHH', data, 1)
    if version != VERSION or length > CHUNK_BYTES or len(data) != 11 + length:
        raise ValueError('bad frame length/version')
    body = data[1:-4]
    if zlib.crc32(body) != struct.unpack_from('<I', data, len(data) - 4)[0]:
        raise ValueError('bad frame CRC')
    return kind, sequence, data[7:-4]


def validate_image(image):
    if len(image) < 8 or len(image) > MAX_IMAGE_BYTES or len(image) % 4:
        raise ValueError('image length must be word-aligned and <= 256 KiB')
    stack, reset = struct.unpack_from('<II', image)
    main_sram = 0x20000000 < stack <= 0x20020000
    ccm_sram = 0x10000000 < stack <= 0x10010000
    if stack % 8 or not (main_sram or ccm_sram):
        raise ValueError('invalid initial MSP')
    if not (reset & 1) or not APP_BASE <= (reset & ~1) < APP_BASE + len(image):
        raise ValueError('invalid Thumb Reset_Handler')


def request_frames(image, firmware_version):
    validate_image(image)
    if not 0 <= firmware_version <= 0xFFFFFFFF:
        raise ValueError('firmware version out of range')
    image_crc = zlib.crc32(image)
    fields = struct.pack('<5I', IMAGE_MAGIC, 1, firmware_version,
                         len(image), image_crc)
    header = fields + struct.pack('<I', zlib.crc32(fields))
    yield START, 0, header
    chunks = (len(image) + CHUNK_BYTES - 1) // CHUNK_BYTES
    for index in range(chunks):
        yield CHUNK, index, image[index * CHUNK_BYTES:(index + 1) * CHUNK_BYTES]
    yield END, chunks, b''


def read_response(transport, deadline):
    while time.monotonic() < deadline:
        first = transport.read(1)
        if not first or first[0] != SOF:
            continue
        header = bytearray(first)
        while len(header) < 7 and time.monotonic() < deadline:
            header.extend(transport.read(7 - len(header)))
        if len(header) != 7:
            continue
        length = struct.unpack_from('<H', header, 5)[0]
        if length > CHUNK_BYTES:
            continue
        while len(header) < 11 + length and time.monotonic() < deadline:
            header.extend(transport.read(11 + length - len(header)))
        if len(header) != 11 + length:
            continue
        try:
            return decode_frame(bytes(header))
        except ValueError:
            continue
    raise TimeoutError('response timed out')


def send_update(transport, image, firmware_version, timeout=1.0, retries=3,
                expect_status=False, status_timeout=30.0):
    if timeout <= 0 or retries < 0 or status_timeout <= 0:
        raise ValueError('invalid retry or timeout setting')
    requests = list(request_frames(image, firmware_version))
    retransmissions = 0
    activated = False
    for kind, sequence, payload in requests:
        frame = encode_frame(kind, sequence, payload)
        for attempt in range(retries + 1):
            written = transport.write(frame)
            if written != len(frame):
                raise OSError('short UART write')
            deadline = time.monotonic() + timeout
            try:
                while True:
                    response_kind, response_seq, response_payload = read_response(
                        transport, deadline)
                    if kind == END and response_seq == sequence and \
                            response_kind == STATUS and len(response_payload) >= 1:
                        if response_payload[0] != 0:
                            raise RuntimeError('activation failed with status '
                                               f'{response_payload[0]}')
                        activated = True
                        break
                    if response_seq == sequence and len(response_payload) == 2 \
                            and response_payload[1] == kind:
                        break
            except TimeoutError:
                if attempt == retries:
                    raise
                retransmissions += 1
                continue
            if activated or response_kind == ACK and response_payload[0] == 0:
                break
            if response_kind == NACK:
                raise RuntimeError(f'receiver rejected {kind}:{sequence} '
                                   f'with status {response_payload[0]}')
            raise RuntimeError('unexpected UART response')
    if expect_status and not activated:
        final_seq = requests[-1][1]
        deadline = time.monotonic() + status_timeout
        while True:
            kind, sequence, payload = read_response(transport, deadline)
            if kind != STATUS or sequence != final_seq or len(payload) < 1:
                continue
            if payload[0] != 0:
                raise RuntimeError(f'activation failed with status {payload[0]}')
            activated = True
            break
    return {'image_bytes': len(image), 'image_crc32': f'0x{zlib.crc32(image):08X}',
            'chunks': (len(image) + CHUNK_BYTES - 1) // CHUNK_BYTES,
            'retransmissions': retransmissions, 'activated': activated}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=pathlib.Path, help='F407 active-slot .bin')
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--port', required=True, help='serial port, e.g. /dev/ttyUSB0')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--timeout', type=float, default=1.0)
    parser.add_argument('--retries', type=int, default=3)
    parser.add_argument('--status-timeout', type=float, default=30.0)
    args = parser.parse_args()
    image = args.image.read_bytes()
    validate_image(image)
    try:
        import serial
    except ImportError as exc:
        raise SystemExit('pyserial is required for a real serial port') from exc
    with serial.Serial(args.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as transport:
        print(send_update(transport, image, args.version,
                          args.timeout, args.retries,
                          expect_status=True,
                          status_timeout=args.status_timeout))


if __name__ == '__main__':
    main()
