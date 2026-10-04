"""Diagnostic Renode check for a valid START and staging-sector erase."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import firmware_updater as protocol  # noqa: E402

OUT = ROOT / 'results/f407-bootloader-update'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='staging-probe-', dir=OUT))
old = (ROOT / 'build-f407/SensorTelemetryF407.bin').read_bytes()
words = [0x424F4F54, 1, 1, 1, 1, len(old), zlib.crc32(old),
         0, 0, 0, 0, 0, 0, 0]
record = struct.pack('<16I', *words,
                     zlib.crc32(struct.pack('<14I', *words)), 0xC0DEC0DE)
(run / 'confirmed-record.bin').write_bytes(record)
new = bytearray([0xFF] * 64)
struct.pack_into('<II', new, 0, 0x20020000, 0x08020009)
kind, sequence, payload = next(protocol.request_frames(bytes(new), 2))
request = protocol.encode_frame(kind, sequence, payload)
uart_log = run / 'boot-uart.bin'
lines = ['using sysbus', 'mach create "f407-staging-probe"',
         'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl',
         'nvic Frequency 16000000',
         'sysbus LoadELF @' + str(ROOT / 'build-f407/SensorTelemetryF407.elf'),
         'sysbus LoadBinary @' + str(run / 'confirmed-record.bin') +
         ' 0x08010000',
         'sysbus LoadELF @' + str(ROOT / 'build-boot-f407/HoodBootF407.elf'),
         'usart1 CreateFileBackend @' + str(uart_log),
         'emulation RunFor "0.05"']
for byte in request:
    lines += [f'usart1 WriteChar 0x{byte:02X}', 'emulation RunFor "0.002"']
lines += ['emulation RunFor "0.5"', 'quit']
script = run / 'staging.resc'
script.write_text('\n'.join(lines) + '\n')
proc = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                       '--console', str(script)], cwd=ROOT,
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                      text=True, timeout=90)
(run / 'console.log').write_text(proc.stdout)
wire = uart_log.read_bytes() if uart_log.exists() else b''
offset = wire.find(bytes([protocol.SOF]))
response = None
if offset >= 0 and len(wire) >= offset + 13:
    try:
        response = protocol.decode_frame(wire[offset:offset + 13])
    except ValueError:
        pass
summary = {'kind': 'renode-staging-start-diagnostic',
           'renode_return_code': proc.returncode,
           'response': None if response is None else
               {'type': response[0], 'sequence': response[1],
                'payload_hex': response[2].hex()},
           'flash_programming_claim': False}
if response == (protocol.ACK, 0, b'\x00\x01'):
    summary['result'] = 'START_ACK_ONLY'
else:
    summary['result'] = 'NOT_VERIFIED'
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run))
print(json.dumps(summary, indent=2))
