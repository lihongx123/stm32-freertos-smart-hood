"""Renode application-side Pending confirmation after health window."""
import json
import pathlib
import re
import struct
import subprocess
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'results/f407-bootloader-update'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='confirm-probe-', dir=OUT))
image = (ROOT / 'build-f407/SensorTelemetryF407.bin').read_bytes()
old = bytearray(image)
old[12] ^= 0x5A
(run / 'backup.bin').write_bytes(old)
words = [0x424F4F54, 1, 1, 3, 2, len(image), zlib.crc32(image),
         1, len(old), zlib.crc32(old), 2, len(image), zlib.crc32(image), 1]
record = struct.pack('<16I', *words,
                     zlib.crc32(struct.pack('<14I', *words)), 0xC0DEC0DE)
(run / 'pending-sector.bin').write_bytes(record + b'\xFF' * (65536 - 64))
uart_log = run / 'app-uart.log'
script = run / 'confirm.resc'
script.write_text('\n'.join([
    'using sysbus', 'mach create "f407-app-confirm"',
    'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl',
    'nvic Frequency 168000000',
    'sysbus LoadELF @' + str(ROOT / 'build-f407/SensorTelemetryF407.elf'),
    'sysbus LoadBinary @' + str(run / 'backup.bin') + ' 0x08060000',
    'sysbus LoadBinary @' + str(run / 'pending-sector.bin') + ' 0x08010000',
    'usart1 CreateFileBackend @' + str(uart_log),
    'emulation RunFor "7"', 'sysbus ReadDoubleWord 0x0801004C', 'quit']) + '\n')
proc = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                       '--console', str(script)], cwd=ROOT,
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                      text=True, timeout=90)
(run / 'console.log').write_text(proc.stdout)
uart = uart_log.read_text(errors='replace') if uart_log.exists() else ''
states = re.findall(r'^0x[0-9a-fA-F]+$', proc.stdout, re.M)
state = int(states[-1], 16) if states else None
summary = {'kind': 'renode-application-confirmation',
           'health_window_ms': 5000,
           'boot_confirm_logged': 'BOOT_CONFIRM,health_window_ms=5000' in uart,
           'second_record_state': state,
           'result': 'PASS' if proc.returncode == 0 and state == 1 and
               'BOOT_CONFIRM,health_window_ms=5000' in uart else 'NOT_VERIFIED',
           'scope': 'application-side software health and Renode metadata append; not physical Flash'}
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run))
print(json.dumps(summary, indent=2))
