"""Deterministic virtual values; no physical sensor model or third-party package."""
import json

SCENARIOS = {
    'idle': (20, 24, 45, 200, 5),
    'light_cooking': (350, 29, 55, 200, 5),
    'heavy_cooking': (700, 35, 65, 200, 5),
    'backflow': (350, 30, 60, 200, -20),
    'dark': (20, 24, 45, 5, 5),
    'sensor_fault': (1200, 24, 45, 200, 5),
    'recovery': (20, 24, 45, 200, 5),
}


def frame(name):
    return ('S,' + ','.join(map(str, SCENARIOS[name])) + '\n').encode('ascii')


class SensorSimulator:
    def __init__(self, uart, log):
        self.uart, self.log = uart, log

    def send(self, data, virtual_time, scenario):
        self.uart.sendall(data)
        self.log.write(json.dumps({'simulation_seconds': virtual_time, 'scenario': scenario,
                                   'bytes': len(data), 'tx': data.decode('ascii')}) + '\n')
        self.log.flush()
