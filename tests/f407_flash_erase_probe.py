"""Quantify Renode erase behavior while exercising the real MCU bootloader."""
import json
import pathlib
import runpy
import subprocess
import time

root = pathlib.Path(__file__).resolve().parents[1]
baseline = runpy.run_path(str(root / 'tests/f407_boot_update_mcu_probe.py'))
run = baseline['run']
script = run / 'mcu-update.resc'
source = script.read_text()
sectors = {5: 0x08020000, 7: 0x08060000, 9: 0x080A0000}
size = 0x20000
loads = []
for sector, address in sectors.items():
    pattern = bytes((i * 13 + sector * 17) & 0xFF for i in range(size))
    path = run / f'sector-{sector}-before.bin'
    path.write_bytes(pattern)
    loads.append(f'sysbus LoadBinary @{path} 0x{address:08X}')
source = source.replace('sysbus LoadBinary @' + str(run / 'old-active.bin'),
                        '\n'.join(loads) + '\nsysbus LoadBinary @' +
                        str(run / 'old-active.bin'), 1)
whole_flash = run / 'flash-after.bin'
dump_lines = [f'flash DumpBinary @{whole_flash}']
source = source.replace('\nquit\n', '\n' + '\n'.join(dump_lines) + '\nquit\n')
probe_script = run / 'sector-probe.resc'
probe_script.write_text(source)
with (run / 'sector-probe-console.log').open('w') as log:
    process = subprocess.Popen(['/home/hello/tools/renode/renode', '--disable-gui',
                                '--console', str(probe_script)], cwd=root,
                               stdout=log, stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 45
    while process.poll() is None:
        if whole_flash.exists() and whole_flash.stat().st_size > 16 * 1024 * 1024:
            process.kill()
            raise RuntimeError('Renode dump exceeded 16 MiB safety limit')
        if time.monotonic() > deadline:
            process.kill()
            raise TimeoutError('Renode dump exceeded 45 seconds')
        time.sleep(0.05)
    return_code = process.returncode
records = []
for sector, address in sectors.items():
    if not whole_flash.exists() or whole_flash.stat().st_size < 0x100000:
        records.append({'sector': sector, 'start': f'0x{address:08X}',
                        'size': size, 'pass': False, 'error': 'dump missing'})
        continue
    with whole_flash.open('rb') as flash_file:
        flash_file.seek(address - 0x08000000)
        data = flash_file.read(size)
    bad = [i for i, byte in enumerate(data) if byte != 0xFF]
    ranges = []
    if bad:
        first = last = bad[0]
        for offset in bad[1:]:
            if offset != last + 1:
                ranges.append([f'0x{address + first:08X}',
                               f'0x{address + last + 1:08X}'])
                first = offset
            last = offset
        ranges.append([f'0x{address + first:08X}',
                       f'0x{address + last + 1:08X}'])
    records.append({'sector': sector, 'start': f'0x{address:08X}',
                    'size': size, 'expected_erased_bytes': size,
                    'actual_erased_bytes': len(data) - len(bad),
                    'first_bad_address': f'0x{address + bad[0]:08X}' if bad else None,
                    'last_bad_address': f'0x{address + bad[-1]:08X}' if bad else None,
                    'bad_byte_count': len(bad), 'bad_ranges': ranges,
                    'pass': (len(data) == size and not bad) if sector == 5 else None,
                    'interpretation': 'erase verification point' if sector == 5
                    else 'post-copy/program snapshot; not an erase acceptance test'})
summary = {'kind': 'renode-production-path-sector-probe',
           'renode_return_code': return_code,
           'note': '5/7/9 exercised by update; sector 0/4 intentionally protected',
           'sectors': records}
(run / 'sector-probe-result.json').write_text(json.dumps(summary, indent=2) + '\n')
(root / 'results/f407_flash_erase_probe.json').write_text(
    json.dumps({'evidence_directory': str(run), **summary}, indent=2) + '\n')
print(json.dumps({'evidence_directory': str(run),
                  'renode_return_code': return_code,
                  'sectors': [{key: value for key, value in record.items()
                               if key != 'bad_ranges'} for record in records]}, indent=2))
