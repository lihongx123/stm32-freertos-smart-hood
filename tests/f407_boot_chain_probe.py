"""Renode check: F407 bootloader validates and starts a confirmed app."""
import json
import pathlib
import re
import struct
import subprocess
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOOT = ROOT / 'build-boot-f407/HoodBootF407.elf'
APP = ROOT / 'build-f407/SensorTelemetryF407.elf'
APP_BIN = ROOT / 'build-f407/SensorTelemetryF407.bin'
OUT = ROOT / 'results/f407-boot-chain'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=OUT))
image = APP_BIN.read_bytes()
assert len(image) >= 8 and len(image) % 4 == 0
words = [0x424F4F54, 1, 1, 1, 1, len(image), zlib.crc32(image) & 0xFFFFFFFF,
         0, 0, 0, 0, 0, 0, 0]
record_crc = zlib.crc32(struct.pack('<14I', *words)) & 0xFFFFFFFF
metadata = struct.pack('<16I', *words, record_crc, 0xC0DEC0DE)
metadata_path = run / 'confirmed-record.bin'
metadata_path.write_bytes(metadata)
symbols = subprocess.check_output(['arm-none-eabi-nm', '-n', str(BOOT)], text=True)
match = re.search(r'^([0-9a-fA-F]+) B boot_status$', symbols, re.M)
assert match, 'boot_status symbol not found'
status_address = int(match.group(1), 16)


def run_case(name, record=None, replacement=None, seconds='0.3'):
    uart_log = run / (name + '-uart.log')
    resc = run / (name + '.resc')
    lines = ['using sysbus', f'mach create "f407-{name}"',
             'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl',
             'nvic Frequency 168000000',
             'sysbus LoadELF @' + str(APP)]
    if record is not None:
        lines.append('sysbus LoadBinary @' + str(record) + ' 0x08010000')
    if replacement is not None:
        lines.append('sysbus LoadBinary @' + str(replacement) + ' 0x08020000')
    lines += ['sysbus LoadELF @' + str(BOOT),
              'usart1 CreateFileBackend @' + str(uart_log),
              f'emulation RunFor "{seconds}"',
              f'sysbus ReadDoubleWord 0x{status_address:08X}', 'quit']
    resc.write_text('\n'.join(lines) + '\n')
    result = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                             '--console', str(resc)], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=60)
    (run / (name + '-console.log')).write_text(result.stdout)
    assert result.returncode == 0 and 'There was an error' not in result.stdout, \
        result.stdout[-2000:]
    log = uart_log.read_text(errors='replace') if uart_log.exists() else ''
    values = re.findall(r'^0x[0-9a-fA-F]+$', result.stdout, re.M)
    assert values, result.stdout[-1200:]
    return log, int(values[-1], 16)


log, _ = run_case('confirmed', metadata_path, seconds='2')
assert 'BOOT' in log and 'RTOS_START' in log and 'COMM_READY' in log, \
    'application did not start after boot selector\n' + log[-1500:]
missing_log, missing_status = run_case('missing-record')
assert 'BOOT' not in missing_log and missing_status == 2, missing_status
bad = bytearray(image)
bad[12] ^= 1
bad_path = run / 'corrupt-app.bin'
bad_path.write_bytes(bad)
corrupt_log, corrupt_status = run_case('corrupt-app', metadata_path, bad_path)
assert 'BOOT' not in corrupt_log and corrupt_status == 3, corrupt_status
bad_record = bytearray(metadata)
bad_record[20] ^= 1
bad_record_path = run / 'corrupt-record.bin'
bad_record_path.write_bytes(bad_record)
bad_record_log, bad_record_status = run_case('corrupt-record', bad_record_path)
assert 'BOOT' not in bad_record_log and bad_record_status == 2, bad_record_status
summary = {'kind': 'renode-boot-chain', 'app_bytes': len(image),
           'app_crc32': f'0x{zlib.crc32(image) & 0xFFFFFFFF:08X}',
           'metadata_state': 'CONFIRMED', 'app_started': True,
           'rejected_cases': ['missing-record', 'corrupt-app', 'corrupt-record'],
           'rejected_status_codes': {'missing-record': missing_status,
                                     'corrupt-app': corrupt_status,
                                     'corrupt-record': bad_record_status},
           'result': 'PASS',
           'scope': 'Renode confirmed-image jump; this probe performs no Flash writes'}
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run))
print(json.dumps(summary, indent=2))
