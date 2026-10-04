"""Renode-only F407 USART1/DMA2 wrap probe; never a hardware claim."""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLATFORM = pathlib.Path('/home/hello/tools/renode/platforms/cpus/stm32f4.repl')
RENODE = pathlib.Path('/home/hello/tools/renode/renode')
ELF = ROOT / 'build-f407/SensorTelemetryF407.elf'
MODEL = ROOT / 'renode/HoodSTM32F4DMA.cs'
OUT = ROOT / 'results/f407-dma-model'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=OUT))
symbols = subprocess.check_output(['arm-none-eabi-nm', '-n', str(ELF)], text=True)
dma_matches = re.findall(r'(?m)^([0-9a-fA-F]+) [bB] dma_rx$', symbols)
assert len(dma_matches) == 1, 'DMA buffer symbol missing or ambiguous'
dma_address = int(dma_matches[0], 16)

original = PLATFORM.read_text()
assert original.count('dma2: DMA.STM32DMA @ sysbus 0x40026400') == 1
assert original.count('usart1: UART.STM32_UART @ sysbus <0x40011000, +0x100>') == 1
platform = original.replace('dma2: DMA.STM32DMA @ sysbus 0x40026400',
                            'dma2: DMA.HoodSTM32F4DMA @ sysbus 0x40026400')
platform = platform.replace('usart1: UART.STM32_UART @ sysbus <0x40011000, +0x100>\n'
                            '    -> nvic@37',
                            'usart1: UART.STM32_UART @ sysbus <0x40011000, +0x100>\n'
                            '    -> nvic@37\n    DMARequest -> dma2@2')
assert 'DMARequest -> dma2@2' in platform
derived = run / 'f407-with-test-dma.repl'
derived.write_text(platform)
uart_log = run / 'uart.log'
script = ['include @' + str(MODEL), 'using sysbus', 'mach create "f407-dma-model"',
          'machine LoadPlatformDescription @' + str(derived),
          'nvic Frequency 168000000', 'sysbus LoadELF @' + str(ELF),
          'usart1 CreateFileBackend @' + str(uart_log),
          'emulation RunFor "1"', 'sysbus ReadDoubleWord 0x40026444']
payload = bytes(range(256)) + b'ABCD'
script += [f'usart1 WriteChar 0x{b:02X}' for b in payload]
script += ['emulation RunFor "2"', 'sysbus ReadDoubleWord 0x40026444']
script += [f'sysbus ReadDoubleWord 0x{dma_address + i:08X}'
           for i in range(0, 256, 4)]
script += ['quit']
resc = run / 'probe.resc'
resc.write_text('\n'.join(script) + '\n')
result = subprocess.run([str(RENODE), '--disable-gui', '--console', str(resc)],
                        cwd=ROOT, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=90)
(run / 'renode-console.log').write_text(result.stdout)
if result.returncode or 'There was an error' in result.stdout or 'Could not tokenize' in result.stdout:
    print('FAIL Renode script/model:', run)
    print(result.stdout[-2500:])
    sys.exit(1)
standalone_hex = [int(x, 16) for x in re.findall(r'(?m)^0x([0-9A-Fa-f]{8})\s*$',
                                                 result.stdout)]
assert len(standalone_hex) >= 66, (run, len(standalone_hex))
ndtr_before, ndtr_after = standalone_hex[-66:-64]
memory_words = standalone_hex[-64:]
memory = b''.join(word.to_bytes(4, 'little') for word in memory_words)
print(f'evidence={run}')
print(f'dma_rx=0x{dma_address:08X}')
print(f'NDTR before={ndtr_before} after={ndtr_after}')
print(f'buffer first-word=0x{memory_words[0]:08X} last-word=0x{memory_words[-1]:08X}')
print('uart tail:')
for line in uart_log.read_text(errors='replace').splitlines():
    if line.startswith(('RXDMA,', 'METRIC,')):
        print(line)
if ndtr_before != 256 or ndtr_after != 252 or memory != b'ABCD' + bytes(range(4, 256)):
    sys.exit('FAIL DMA wrap count or 256-byte memory image')
if ('RXDMA,events=3,bytes=260,errors=0,fallback=0' not in
        uart_log.read_text(errors='replace')):
    sys.exit('FAIL application did not consume all 260 DMA bytes')
print('PASS simulated DMA2 circular wrap for 260 input bytes')
