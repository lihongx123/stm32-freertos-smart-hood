"""Inject simulation-only motor stall and feedback loss into F407 Renode."""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'results/f407-motor-fault'
OUT.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=OUT))
uart_log = run / 'uart.log'
script = ['using sysbus', 'mach create "f407-motor-fault"',
          'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl',
          'machine LoadPlatformDescription @' + str(ROOT / 'renode/f407-dma-edge.repl'),
          'nvic Frequency 168000000',
          'sysbus LoadELF @' + str(ROOT / 'build-f407/SensorTelemetryF407.elf'),
          'usart1 CreateFileBackend @' + str(uart_log),
          'emulation RunFor "1"']


def send(payload):
    script.extend(f'usart1 WriteChar 0x{byte:02X}' for byte in payload)


sensor = b'S,400,25,50,50,0\n'
for _ in range(3):
    send(sensor)
    script.append('emulation RunFor "1"')
send(b'M,STALL,1\n')
send(sensor)
script.append('emulation RunFor "1.2"')
send(b'M,STALL,0\n')
send(sensor)
script.append('emulation RunFor "1.2"')
send(b'M,HALL_LOSS,1\n')
send(sensor)
script.append('emulation RunFor "1.2"')
send(b'M,HALL_LOSS,0\n')
send(sensor)
script.extend(['emulation RunFor "1.2"', 'quit'])
resc = run / 'probe.resc'
resc.write_text('\n'.join(script) + '\n')
result = subprocess.run(['/home/hello/tools/renode/renode', '--disable-gui',
                         '--console', str(resc)], cwd=ROOT, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=90)
(run / 'renode-console.log').write_text(result.stdout)
if result.returncode or 'There was an error' in result.stdout:
    print('FAIL Renode execution:', run)
    print(result.stdout[-1500:])
    sys.exit(1)
log = uart_log.read_text(errors='replace')
motor = [dict((key, int(value)) for key, value in
              re.findall(r'([a-z_]+)=(\d+)', line))
         for line in log.splitlines() if line.startswith('MOTOR,SIM,')]
print('evidence=' + str(run))
for line in log.splitlines():
    if line.startswith(('MOTOR,SIM,', 'TEST_', 'FAULT,mask=')):
        print(line)
assert 'TEST_MOTOR_STALL,1' in log and 'TEST_MOTOR_STALL,0' in log
assert 'TEST_HALL_LOSS,1' in log and 'TEST_HALL_LOSS,0' in log
assert any(m.get('fault_mask') == 1 and m.get('duty_permyriad') == 0
           and m.get('hall_rpm') == 0 and
           m.get('stall_events') == 1 for m in motor)
assert any(m.get('fault_mask') == 2 and m.get('duty_permyriad') == 0
           and m.get('hall_rpm') == 0 and
           m.get('feedback_loss_events') == 1 for m in motor)
assert any(m.get('fault_mask') == 0 and m.get('duty_permyriad', 0) > 0
           and m.get('hall_rpm', 0) > 0 for m in motor)
assert 'HEALTH,stalled=' not in log
print('PASS F407 software fault injection, safe output and recovery')
