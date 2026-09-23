"""Extract measured values from saved UART logs, without manufacturing counters."""
import json
import hashlib
import pathlib
import re
import sys

run = pathlib.Path(sys.argv[1])
text = (run / 'scenario-run.log').read_text()
summary = json.loads((run / 'validation-summary.json').read_text())
manifest = json.loads((run / 'manifest.json').read_text())
root = pathlib.Path(__file__).resolve().parents[1]
assert hashlib.sha256((run / 'SensorTelemetry.elf').read_bytes()).hexdigest() == manifest['elf_sha256']
assert all(hashlib.sha256((root / name).read_bytes()).hexdigest() == digest
           for name, digest in manifest['source_sha256'].items()), 'source changed since validation'
def records(prefix):
    return [dict((key, int(value)) for key, value in re.findall(r'(\w+)=(\d+)', line))
            for line in text.splitlines() if line.startswith(prefix + ',')]
memory = records('MEM')
metrics = {
    'validation_passed': summary['passed'],
    'source_and_elf_fingerprints_verified': True,
    'assertions_passed': sum(item['pass'] for item in summary['checks']),
    'assertions_total': len(summary['checks']),
    'simulation_seconds': summary['simulation_seconds'],
    'last_periodic_counters': records('METRIC')[-1],
    'last_periodic_status': records('STATUS')[-1],
    'min_free_heap_bytes': min(item['free_heap'] for item in memory),
    'min_stack_free_words': {key: min(item[key] for item in memory)
                             for key in memory[0] if key.startswith('stack_')},
    'size_output': (run / 'size.txt').read_text(),
    'scope': 'Periodic UART snapshots; fault/overflow counters include deliberate injections; no physical hardware.',
}
(run / 'measured-metrics.json').write_text(json.dumps(metrics, indent=2))
print(json.dumps(metrics, indent=2))
