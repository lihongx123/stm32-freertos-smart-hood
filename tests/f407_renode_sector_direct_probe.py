"""Isolated Renode STM32F4 FLASH_CR sector erase geometry experiment."""
import json
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
test_model = '--test-model' in sys.argv
sectors = {4: (0x10000, 0x10000), 5: (0x20000, 0x20000),
           9: (0xA0000, 0x20000)}
out = root / 'results/f407-bootloader-update'
out.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='sector-direct-', dir=out))
platform = pathlib.Path('/home/hello/tools/renode/platforms/cpus/stm32f4.repl')
model_lines = []
if test_model:
    description = platform.read_text()
    assert description.count('flash: Memory.MappedMemory @ sysbus 0x08000000\n    size: 0x200000') == 1
    assert description.count('flash_controller: MTD.STM32F4_FlashController @ {') == 1
    description = description.replace('flash: Memory.MappedMemory @ sysbus 0x08000000\n    size: 0x200000',
                                      'flash: Memory.MappedMemory @ sysbus 0x08000000\n    size: 0x100000')
    description = description.replace('flash_controller: MTD.STM32F4_FlashController @ {',
                                      'flash_controller: MTD.HoodSTM32F407Flash @ {')
    platform = run / 'f407-test-flash.repl'
    platform.write_text(description)
    model_lines = ['include @' + str(root / 'renode/HoodSTM32F407Flash.cs')]
records = []
for sector, (offset, size) in sectors.items():
    pattern = bytes((i * 13 + sector * 17) & 0x7F for i in range(size))
    before = run / f'sector-{sector}-before.bin'
    before.write_bytes(pattern)
    guard = run / f'sector-{sector}-guard.bin'
    guard.write_bytes(b'\x12\x34\x56\x78')
    after = run / f'sector-{sector}-flash.bin'
    cr_base = 0x2 | (sector << 3) | (2 << 8)  # SER/SNB/PSIZE=word
    script = run / f'sector-{sector}.resc'
    script.write_text('\n'.join(model_lines + [
        'using sysbus', f'mach create "sector-{sector}"',
        'machine LoadPlatformDescription @' + str(platform),
        f'sysbus LoadBinary @{before} 0x{0x08000000 + offset:08X}',
        f'sysbus LoadBinary @{guard} 0x{0x08000000 + offset - 4:08X}',
        f'sysbus LoadBinary @{guard} 0x{0x08000000 + offset + size:08X}',
        'sysbus WriteDoubleWord 0x40023C04 0x45670123',
        'sysbus WriteDoubleWord 0x40023C04 0xCDEF89AB',
        f'sysbus WriteDoubleWord 0x40023C10 0x{cr_base:08X}',
        f'sysbus WriteDoubleWord 0x40023C10 0x{cr_base | 0x10000:08X}',
        f'flash DumpBinary @{after}', 'quit']) + '\n')
    result = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                             '--console', str(script)], cwd=root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30)
    (run / f'sector-{sector}-console.log').write_text(result.stdout)
    if not after.exists() or after.stat().st_size > 4 * 1024 * 1024:
        raise RuntimeError(f'sector {sector}: missing/oversize dump')
    with after.open('rb') as image:
        image.seek(offset - 4)
        left_guard = image.read(4)
        image.seek(offset)
        data = image.read(size)
        right_guard = image.read(4)
    bad = [i for i, value in enumerate(data) if value != 0xFF]
    records.append({'sector': sector, 'start': f'0x{0x08000000 + offset:08X}',
                    'size': size, 'expected_erased_bytes': size,
                    'actual_erased_bytes': len(data) - len(bad),
                    'first_bad_address': f'0x{0x08000000 + offset + bad[0]:08X}' if bad else None,
                    'last_bad_address': f'0x{0x08000000 + offset + bad[-1]:08X}' if bad else None,
                    'bad_byte_count': len(bad),
                    'adjacent_unchanged': left_guard == guard.read_bytes()
                    and right_guard == guard.read_bytes(),
                    'pass': result.returncode == 0 and len(data) == size and not bad
                    and left_guard == guard.read_bytes()
                    and right_guard == guard.read_bytes()})
summary = {'kind': 'renode-direct-flash-register-sector-probe',
           'model': 'repository-test-only' if test_model else 'stock-renode',
           'method': 'FLASH_KEYR unlock, CR SER/SNB/PSIZE/STRT; no CPU firmware',
           'sectors': records}
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run))
print(json.dumps(summary, indent=2))
