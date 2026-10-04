"""Renode software watchdog-gate test with a stalled SensorTask."""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'results/f407-watchdog'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=OUT))
uart_log = run / 'uart.log'
script = ['using sysbus', 'mach create "f407-watchdog-gate"',
          'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl',
          'machine LoadPlatformDescription @' + str(ROOT / 'renode/f407-dma-edge.repl'),
          'nvic Frequency 168000000',
          'sysbus LoadELF @' + str(ROOT / 'build-f407/SensorTelemetryF407.elf'),
          'usart1 CreateFileBackend @' + str(uart_log),
          'emulation RunFor "1"']
script += [f'usart1 WriteChar 0x{byte:02X}' for byte in b'T,1,2500\n']
script += ['emulation RunFor "5"', 'quit']
resc = run / 'probe.resc'
resc.write_text('\n'.join(script) + '\n')
result = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                         '--console', str(resc)], cwd=ROOT, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=60)
(run / 'renode-console.log').write_text(result.stdout)
if result.returncode or 'There was an error' in result.stdout:
    print('FAIL Renode execution:', run)
    print(result.stdout[-1500:])
    sys.exit(1)
log = uart_log.read_text(errors='replace')
lines = [line for line in log.splitlines()
         if line.startswith(('TEST_STALL,', 'HEALTH,', 'WATCHDOG,SIM,'))]
print('evidence=' + str(run))
for line in lines:
    print(line)
assert 'TEST_STALL,task=1,ms=2500' in log
assert 'HEALTH,stalled=2' in log
assert 'HEALTH,stalled=0' in log
skips = [int(value) for value in re.findall(r'WATCHDOG,SIM,eligible=\d+,skipped=(\d+)', log)]
assert skips and max(skips) > 0
assert 'HEALTH,stalled=2' in log and log.rfind('HEALTH,stalled=0') > log.find('HEALTH,stalled=2')
print('PASS simulated watchdog gate skips stale task and resumes after recovery')
