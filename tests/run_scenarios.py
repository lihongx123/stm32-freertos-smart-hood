#!/usr/bin/env python3
"""Build and validate real UART output from headless Renode, fail on assertions."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from sensor_sim import SensorSimulator, frame, SCENARIOS


def connect(port, process):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError('Renode exited before connection; see console log')
        try:
            result = socket.create_connection(('127.0.0.1', port), timeout=1)
            result.settimeout(10)
            return result
        except OSError:
            time.sleep(.1)
    raise TimeoutError(f'port {port} unavailable')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='results/scenarios-01')
    parser.add_argument('--firmware-mode', choices=('dma', 'irq-fallback'), default='dma')
    parser.add_argument('--corrected-dma-model', action='store_true',
                        help='Use project-local EN-readback fix and isolate HAL tick DMA wiring')
    parser.add_argument('--renode', default=shutil.which('renode') or str(pathlib.Path.home() / 'tools/renode/renode'))
    args = parser.parse_args()
    os.chdir(ROOT)
    output = ROOT / args.output
    output.mkdir(parents=True, exist_ok=False)
    build_dir = 'build' if args.firmware_mode == 'dma' else 'build-irq-functional'
    make_args = ['make', '-j' + str(os.cpu_count()), 'BUILD_DIR=' + build_dir]
    if args.firmware_mode == 'irq-fallback':
        make_args.append('C_DEFS=-DUSE_HAL_DRIVER -DSTM32F103xB -DAPP_UART_RX_IT_FALLBACK=1')
    with (output / 'build.log').open('w') as log:
        subprocess.run(make_args, stdout=log, stderr=subprocess.STDOUT, check=True)
    elf = build_dir + '/SensorTelemetry.elf'
    (output / 'size.txt').write_text(subprocess.check_output(['arm-none-eabi-size', elf], text=True))
    shutil.copyfile(elf, output / 'SensorTelemetry.elf')
    script = output / 'run.resc'
    script_text=(ROOT/'renode/hood.resc').read_text().replace('@build/SensorTelemetry.elf', '@'+elf)
    if args.corrected_dma_model:
        platform_path=pathlib.Path(args.renode).resolve().parent/'platforms/cpus/stm32f103.repl'
        platform=platform_path.read_text()
        assert platform.count('UpdateInterrupt -> nvic@25 | dma1@5') == 1
        platform=platform.replace('UpdateInterrupt -> nvic@25 | dma1@5','UpdateInterrupt -> nvic@25')
        platform=platform.replace('dma1: DMA.STM32G0DMA','dma1: DMA.HoodSTM32DMA')
        local_platform=output/'corrected-platform.repl'
        local_platform.write_text(platform)
        script_text='include @renode/HoodSTM32DMA.cs\n'+script_text.replace(
            '@platforms/cpus/stm32f103.repl','@'+str(local_platform))
    script.write_text(script_text)
    sources = list((ROOT / 'Core/Src').glob('*.c')) + list((ROOT / 'Core/Inc').glob('*.h'))
    sources += [ROOT/'Makefile', ROOT/'renode/hood.resc', ROOT/'tools/sensor_sim.py', pathlib.Path(__file__).resolve()]
    if args.corrected_dma_model:
        sources.append(ROOT/'renode/HoodSTM32DMA.cs')
    (output / 'manifest.json').write_text(json.dumps({
        'elf_sha256': hashlib.sha256((output/'SensorTelemetry.elf').read_bytes()).hexdigest(),
        'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'renode': args.renode, 'simulation': True, 'firmware_mode': args.firmware_mode,
        'corrected_dma_model': args.corrected_dma_model}, indent=2))
    checks = []
    process = None
    monitor = uart = None
    text = ''
    virtual = 0.0
    console = (output / 'renode-console.log').open('w')
    uart_log = (output / 'scenario-run.log').open('w')
    tx_log = (output / 'sensor-tx.jsonl').open('w')
    monitor_log = (output / 'monitor.log').open('w')
    try:
        # Refuse to connect to somebody else's process on the fixed local ports.
        for port in (12345, 12346):
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', port))
        process = subprocess.Popen([args.renode, '--disable-gui', '--plain', '--port', '12345', str(script)],
                                   stdout=console, stderr=subprocess.STDOUT)
        monitor = connect(12345, process)

        def prompt():
            data = b''
            while b'(smart-hood)' not in data:
                chunk = monitor.recv(65536)
                if not chunk:
                    raise RuntimeError('Renode monitor closed')
                data += chunk
            decoded = data.decode('utf-8', errors='replace')
            monitor_log.write(decoded)
            monitor_log.flush()
            if 'There was an error' in decoded:
                raise RuntimeError(decoded)

        prompt()
        uart = connect(12346, process)
        simulator = SensorSimulator(uart, tx_log)

        def advance(seconds):
            nonlocal virtual, text
            monitor.sendall(f'emulation RunFor "{seconds}"\n'.encode())
            prompt()
            virtual += seconds
            uart.settimeout(.05)
            while True:
                try:
                    data = uart.recv(65536)
                    if not data:
                        raise RuntimeError('UART endpoint closed')
                    decoded = data.decode('ascii', errors='replace')
                    text += decoded
                    uart_log.write(decoded)
                except socket.timeout:
                    break
            uart_log.flush()

        def check(name, passed):
            checks.append({'name': name, 'pass': bool(passed), 'simulation_seconds': round(virtual, 3)})
            print(('PASS ' if passed else 'FAIL ') + name, flush=True)
            if not passed:
                raise AssertionError(name)

        def scenario(name, seconds=2.0):
            begin = len(text)
            for _ in range(round(seconds/.2)):
                simulator.send(frame(name), virtual, name)
                advance(.2)
            return text[begin:]

        advance(.5)
        check('boot', 'BOOT' in text and 'RTOS_START' in text and 'COMM_READY' in text)
        scenario('idle')
        check('RTOS heartbeat', text.count('HEARTBEAT seq=') >= 2)
        check('idle NORMAL', 'STATE,NORMAL' in text)
        check('light cooking MEDIUM', 'FAN,MEDIUM' in scenario('light_cooking'))
        check('heavy cooking HIGH', 'FAN,HIGH' in scenario('heavy_cooking'))
        segment = scenario('backflow')
        check('backflow warning/BOOST', 'STATE,WARNING' in segment and 'FAN,BOOST' in segment)
        check('dark light ON', 'LIGHT,ON' in scenario('dark'))
        check('invalid sensor FAULT', 'STATE,FAULT' in scenario('sensor_fault'))
        segment = scenario('recovery', 3)
        check('recovery hysteresis', 'STATE,RECOVERY' in segment and 'STATE,NORMAL' in segment and
              segment.index('STATE,RECOVERY') < segment.index('STATE,NORMAL'))
        begin = len(text)
        advance(2.5)
        check('communication/sensor timeout', 'STATE,FAULT' in text[begin:] and 'FAULT,mask=5' in text[begin:])
        scenario('recovery', 3)
        # Fragmentation and burst: actual ISR/RX path, not a parser-only call.
        before = [int(n) for n in re.findall(r'frame_ok=(\d+)', text)][-1]
        data = frame('idle')
        simulator.send(data[:7], virtual, 'fragment-1'); advance(.1)
        simulator.send(data[7:] + data + data, virtual, 'fragment-2-and-burst'); advance(1)
        after = [int(n) for n in re.findall(r'frame_ok=(\d+)', text)][-1]
        check('fragmented frame and three-frame burst', after >= before+3)
        begin = len(text)
        simulator.send(b'S,bad\n', virtual, 'malformed'); advance(.5)
        check('malformed rejected', 'STATE,FAULT' in text[begin:] and 'FAULT,mask=2' in text[begin:])
        scenario('recovery', 3)
        begin = len(text)
        simulator.send(frame('idle').rstrip(b'\n')+b'\x00hidden\n',virtual,'embedded-NUL'); advance(.5)
        check('embedded NUL rejected', 'STATE,FAULT' in text[begin:])
        scenario('recovery', 3)
        begin = len(text)
        simulator.send(b'S,'+b'1'*120+b'\n',virtual,'overlong'); advance(.5)
        check('overlong frame rejected', 'STATE,FAULT' in text[begin:])
        scenario('recovery', 3)
        # Pause CommTask while interrupts continue to exercise actual ring overflow.
        simulator.send(b'T,0,500\n', virtual, 'stall-comm'); advance(.1)
        simulator.send(b'x'*1024 + b'\n', virtual, 'overflow'); advance(.8)
        simulator.send(b'\n', virtual, 'resync'); advance(.2)
        scenario('recovery', 3)
        check('ring overflow detected', max(map(int, re.findall(r'ring_overflow=(\d+)', text))) > 0)
        begin = len(text)
        simulator.send(b'T,2,2200\n', virtual, 'stall-control')
        scenario('idle', 3)
        check('software watchdog detects stalled ControlTask', 'HEALTH,stalled=4' in text[begin:])
        check('queue overflow detected', max(map(int, re.findall(r'queue_drop=(\d+)', text))) > 0)
        scenario('recovery', 4)
        final_status = re.findall(r'STATUS,[^\r\n]+', text)[-1]
        check('final NORMAL recovery', 'state=1,' in final_status and 'fault_mask=0' in final_status)
        check('all task stacks have headroom', all(int(n)>0 for n in re.findall(r'stack_\w+=(\d+)', text)))
    except Exception as error:
        checks.append({'name': 'execution', 'pass': False, 'error': str(error)})
        print('FAIL', error, flush=True)
    finally:
        if monitor:
            try: monitor.sendall(b'quit\n')
            except OSError: pass
            monitor.close()
        if uart: uart.close()
        if process:
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        for log in (console, uart_log, tx_log, monitor_log): log.close()
        passed = bool(checks) and all(item['pass'] for item in checks)
        result = {'passed': passed, 'checks': checks, 'simulation_seconds': virtual,
                  'firmware_mode': args.firmware_mode, 'dma_hardware_verified': False,
                  'corrected_dma_model': args.corrected_dma_model}
        (output / 'validation-summary.json').write_text(json.dumps(result, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
