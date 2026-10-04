"""Renode bootloader UART framing check without Flash writes."""
import json
import pathlib
import re
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
run = pathlib.Path(tempfile.mkdtemp(prefix='uart-probe-', dir=OUT))
image = (ROOT / 'build-f407/SensorTelemetryF407.bin').read_bytes()
words = [0x424F4F54, 1, 1, 1, 1, len(image), zlib.crc32(image),
         0, 0, 0, 0, 0, 0, 0]
metadata = struct.pack('<16I', *words,
                       zlib.crc32(struct.pack('<14I', *words)), 0xC0DEC0DE)
(run / 'confirmed-record.bin').write_bytes(metadata)
fields = struct.pack('<5I', protocol.IMAGE_MAGIC, 1, 2, 0, 0)
bad_header = fields + struct.pack('<I', zlib.crc32(fields))
request = protocol.encode_frame(protocol.START, 0, bad_header)
uart_log = run / 'boot-uart.bin'
script = run / 'boot-uart.resc'
lines = ['using sysbus', 'mach create "f407-boot-uart"',
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
lines += ['emulation RunFor "0.2"', 'quit']
script.write_text('\n'.join(lines) + '\n')
result = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                         '--console', str(script)], cwd=ROOT,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, timeout=90)
(run / 'console.log').write_text(result.stdout)
assert result.returncode == 0 and 'There was an error' not in result.stdout, \
    result.stdout[-3000:]
wire = uart_log.read_bytes()
offset = wire.find(bytes([protocol.SOF]))
assert offset >= 0, wire[:100]
response = wire[offset:offset + 13]
kind, sequence, payload = protocol.decode_frame(response)
assert kind == protocol.NACK and sequence == 0 and payload == b'\x02\x01', \
    (kind, sequence, payload)
summary = {'kind': 'renode-bootloader-uart-header-rejection',
           'bad_declared_size': 0, 'nack_code': 2,
           'nack_request_type': protocol.START,
           'flash_write_attempted': False, 'result': 'PASS',
           'scope': 'bootloader USART1 receive/framing only; no MCU Flash programming'}
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run))
print(json.dumps(summary, indent=2))
