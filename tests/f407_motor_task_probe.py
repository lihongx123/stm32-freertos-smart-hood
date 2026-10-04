"""Run the F407 firmware's software motor loop under Renode."""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'results/f407-motor-firmware'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=OUT))
uart_log = run / 'uart.log'
script = ['using sysbus', 'mach create "f407-motor-software"',
          'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl',
          'machine LoadPlatformDescription @' + str(ROOT / 'renode/f407-dma-edge.repl'),
          'nvic Frequency 168000000',
          'sysbus LoadELF @' + str(ROOT / 'build-f407/SensorTelemetryF407.elf'),
          'usart1 CreateFileBackend @' + str(uart_log),
          'emulation RunFor "1"']


def frame(payload):
    return [f'usart1 WriteChar 0x{byte:02X}' for byte in payload]


for _ in range(5):
    script += frame(b'S,400,25,50,50,0\n')
    script += ['emulation RunFor "1"']
script += frame(b'S,0,25,50,50,0\n')
script += ['emulation RunFor "1.2"', 'quit']
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
motor = [line for line in log.splitlines() if line.startswith('MOTOR,SIM,')]
parsed = []
for line in motor:
    values = dict((key, int(value)) for key, value in
                  re.findall(r'([a-z_]+)=(\d+)', line))
    parsed.append(values)
frames = [int(value) for value in re.findall(r'frame_ok=(\d+)', log)]
print('evidence=' + str(run))
print('max_frame_ok=' + str(max(frames, default=0)))
for item in parsed:
    print('motor=' + str(item))
assert 'MOTOR_SIM_READY' in log
assert max(frames, default=0) >= 6
assert any(item.get('target_rpm') == 1400 and
           item.get('actual_rpm', 0) >= 1200 and
           item.get('hall_rpm', 0) >= 1200 and
           item.get('hall_edges', 0) >= 100 and
           abs(item.get('actual_rpm', 0) - item.get('hall_rpm', 0)) < 100 and
           item.get('duty_permyriad', 0) > 0 for item in parsed)
assert parsed[-1].get('target_rpm') == 0
assert parsed[-1].get('duty_permyriad') == 0
assert 'HEALTH,stalled=' not in log
print('PASS F407 Renode sensor-to-Hall-to-PID software loop and safe OFF')
