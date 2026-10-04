"""Host-only first-order motor plant exercising the actual C PID module."""
import ctypes
import hashlib
import json
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DT = 0.02


class PidConfig(ctypes.Structure):
    _fields_ = [(name, ctypes.c_float) for name in
                ('kp', 'ki', 'kd', 'integral_min', 'integral_max',
                 'output_min', 'output_max')]


class PidController(ctypes.Structure):
    _fields_ = [('config', PidConfig), ('integral', ctypes.c_float),
                ('previous_error', ctypes.c_float),
                ('saturation_count', ctypes.c_uint32),
                ('has_previous', ctypes.c_bool)]


class MotorPlant:
    def __init__(self):
        self.rpm = 0.0
        self.max_rpm = 3000.0
        self.tau_seconds = 0.30

    def step(self, duty, load_factor=1.0, stalled=False):
        equilibrium = 0.0 if stalled else duty * self.max_rpm * load_factor
        self.rpm += (equilibrium - self.rpm) * DT / self.tau_seconds
        return self.rpm


with tempfile.TemporaryDirectory(prefix='hood-motor-') as build_dir:
    lib_path = pathlib.Path(build_dir) / 'libmotor_control.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-I' + str(ROOT / 'Core/Inc'),
                    str(ROOT / 'Core/Src/motor_control.c'), '-lm', '-o', str(lib_path)],
                   check=True)
    lib = ctypes.CDLL(str(lib_path))
    lib.pid_init.argtypes = [ctypes.POINTER(PidController), ctypes.POINTER(PidConfig)]
    lib.pid_init.restype = ctypes.c_bool
    lib.pid_update.argtypes = [ctypes.POINTER(PidController), ctypes.c_float,
                                ctypes.c_float, ctypes.c_float, ctypes.c_bool]
    lib.pid_update.restype = ctypes.c_float
    config = PidConfig(0.00035, 0.0005, 0.0, -2000.0, 2000.0, 0.0, 1.0)
    pid = PidController()
    assert lib.pid_init(ctypes.byref(pid), ctypes.byref(config))
    motor = MotorPlant()
    results = []

    def run_segment(name, target, duration=4.0, load=1.0,
                    stalled=False, feedback_valid=True):
        samples = []
        for _ in range(round(duration / DT)):
            duty = lib.pid_update(ctypes.byref(pid), target, motor.rpm,
                                  DT, feedback_valid)
            rpm = motor.step(duty, load, stalled)
            samples.append((rpm, duty))
        result = {'scenario': name, 'target_rpm': target,
                  'end_rpm': round(samples[-1][0], 1),
                  'end_duty': round(samples[-1][1], 4),
                  'peak_duty': round(max(s[1] for s in samples), 4),
                  'saturation_count': pid.saturation_count}
        results.append(result)
        return result

    start = run_segment('start-from-zero', 900)
    up = run_segment('low-to-high', 1800)
    down = run_segment('high-to-low', 900)
    load = run_segment('load-disturbance', 900, load=0.7)
    stall = run_segment('motor-stall-feedback-lost', 900, duration=1.0,
                        stalled=True, feedback_valid=False)
    recovery = run_segment('recovery', 900)
    off = run_segment('safe-off', 0, duration=1.0)

    assert abs(start['end_rpm'] - 900) < 100
    assert abs(up['end_rpm'] - 1800) < 150
    assert abs(down['end_rpm'] - 900) < 100
    assert abs(load['end_rpm'] - 900) < 120
    assert stall['end_duty'] == 0
    assert abs(recovery['end_rpm'] - 900) < 100
    assert off['end_duty'] == 0 and off['end_rpm'] < 100
    summary = {'kind': 'host-motor-model', 'dt_seconds': DT,
               'max_rpm': motor.max_rpm,
               'time_constant_seconds': motor.tau_seconds,
               'pid_source_sha256': hashlib.sha256(
                   (ROOT / 'Core/Src/motor_control.c').read_bytes()).hexdigest(),
               'scenarios': results, 'result': 'PASS'}
    output_root = ROOT / 'results/f407-motor-model'
    output_root.mkdir(parents=True, exist_ok=True)
    output_dir = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=output_root))
    (output_dir / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'evidence={output_dir / "result.json"}')
    print(json.dumps(summary, indent=2))
