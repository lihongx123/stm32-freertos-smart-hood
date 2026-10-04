"""Renode MCU Flash path with two tiny executable Thumb loop images."""
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
run = pathlib.Path(tempfile.mkdtemp(prefix='mcu-update-', dir=OUT))
test_model = '--test-model' in sys.argv
if test_model:
    stock_platform = pathlib.Path('/home/hello/tools/renode/platforms/cpus/stm32f4.repl')
    platform = stock_platform.read_text()
    assert platform.count('flash: Memory.MappedMemory @ sysbus 0x08000000\n    size: 0x200000') == 1
    assert platform.count('flash_controller: MTD.STM32F4_FlashController @ {') == 1
    platform = platform.replace('flash: Memory.MappedMemory @ sysbus 0x08000000\n    size: 0x200000',
                                'flash: Memory.MappedMemory @ sysbus 0x08000000\n    size: 0x100000')
    platform = platform.replace('flash_controller: MTD.STM32F4_FlashController @ {',
                                'flash_controller: MTD.HoodSTM32F407Flash @ {')
    derived_platform = run / 'f407-test-flash.repl'
    derived_platform.write_text(platform)


def tiny_image(marker):
    image = bytearray([0xFF] * 64)
    struct.pack_into('<II', image, 0, 0x20020000, 0x08020009)
    image[8:10] = b'\xFE\xE7'  # Thumb: branch to self
    struct.pack_into('<I', image, 20, marker)
    return bytes(image)


old = tiny_image(0x11223344)
new = tiny_image(0x55667788)
(run / 'old-active.bin').write_bytes(old)
record_words = [0x424F4F54, 1, 1, 1, 1, len(old), zlib.crc32(old),
                0, 0, 0, 0, 0, 0, 0]
record = struct.pack('<16I', *record_words,
                     zlib.crc32(struct.pack('<14I', *record_words)),
                     0xC0DEC0DE)
(run / 'confirmed-record.bin').write_bytes(record + b'\xFF' * (65536 - 64))
frames = [protocol.encode_frame(*request)
          for request in protocol.request_frames(new, 2)]
uart_log = run / 'boot-uart.bin'
lines = (['include @' + str(ROOT / 'renode/HoodSTM32F407Flash.cs')]
         if test_model else []) + ['using sysbus', 'mach create "f407-mcu-update"',
         'machine LoadPlatformDescription @' +
         (str(derived_platform) if test_model else 'platforms/cpus/stm32f4.repl'),
         'nvic Frequency 16000000',
         'sysbus LoadBinary @' + str(run / 'old-active.bin') + ' 0x08020000',
         'sysbus LoadBinary @' + str(run / 'confirmed-record.bin') +
         ' 0x08010000',
         'sysbus LoadELF @' + str(ROOT / 'build-boot-f407/HoodBootF407.elf'),
         'usart1 CreateFileBackend @' + str(uart_log),
         'emulation RunFor "0.05"']
for frame in frames:
    for byte in frame:
        lines += [f'usart1 WriteChar 0x{byte:02X}',
                  'emulation RunFor "0.002"']
    lines += ['emulation RunFor "0.02"']
lines += ['emulation RunFor "0.5"',
          'sysbus ReadDoubleWord 0x080A0000',
          'sysbus ReadDoubleWord 0x08020000',
          'sysbus ReadDoubleWord 0x08020014',
          'sysbus ReadDoubleWord 0x08060000',
          'sysbus ReadDoubleWord 0x08060014',
          'sysbus ReadDoubleWord 0x0801008C',
          'quit']
script = run / 'mcu-update.resc'
script.write_text('\n'.join(lines) + '\n')
result = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                         '--console', str(script)], cwd=ROOT,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, timeout=120)
(run / 'console.log').write_text(result.stdout)
wire = uart_log.read_bytes() if uart_log.exists() else b''
parsed = []
cursor = 0
while cursor < len(wire):
    start = wire.find(bytes([protocol.SOF]), cursor)
    if start < 0 or start + 11 > len(wire):
        break
    payload_bytes = struct.unpack_from('<H', wire, start + 5)[0]
    end = start + 11 + payload_bytes
    if end > len(wire):
        break
    try:
        parsed.append(protocol.decode_frame(wire[start:end]))
        cursor = end
    except ValueError:
        cursor = start + 1
values = re.findall(r'^0x[0-9a-fA-F]+$', result.stdout, re.M)
memory_words = [int(value, 16) for value in values[-6:]]
summary = {'kind': 'renode-mcu-update-diagnostic',
           'flash_model': 'repository-test-only-f407-geometry' if test_model else 'stock-renode',
           'renode_return_code': result.returncode,
           'uart_frames': [{'type': kind, 'sequence': seq,
                            'payload_hex': payload.hex()}
                           for kind, seq, payload in parsed],
           'memory_words': [f'0x{word:08X}' for word in memory_words],
           'expected_active_word': '0x55667788',
           'expected_backup_word': '0x11223344',
           'scope': '64-byte executable loop images in Renode; not physical Flash'}
status_payloads = [payload for kind, _, payload in parsed
                   if kind == protocol.STATUS]
if status_payloads and len(status_payloads[-1]) >= 7:
    payload = status_payloads[-1]
    summary['activation_diagnostic'] = {
        'result_code': payload[0], 'step_code': payload[1],
        'flash_error_code': payload[2],
        'flash_error_address': f'0x{struct.unpack_from("<I", payload, 3)[0]:08X}'}
summary['result'] = 'PASS' if (
    result.returncode == 0 and len(memory_words) == 6 and
    memory_words[2] == 0x55667788 and memory_words[4] == 0x11223344 and
    any(kind == protocol.STATUS and payload and payload[0] == 0
        for kind, _, payload in parsed)) else 'NOT_VERIFIED'
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run))
print(json.dumps(summary, indent=2))
