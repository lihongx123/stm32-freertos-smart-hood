"""Capture one real command, preserving output/exit code without overwriting evidence."""
import json
import pathlib
import subprocess
import sys
import time

log = pathlib.Path(sys.argv[1])
log.parent.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
with log.open('x') as output:
    process = subprocess.run(sys.argv[2:], stdout=output, stderr=subprocess.STDOUT)
log.with_suffix(log.suffix + '.json').write_text(json.dumps({
    'command': sys.argv[2:], 'exit_code': process.returncode,
    'elapsed_seconds': time.monotonic() - started}, indent=2))
print(f'{log}: exit={process.returncode}')
sys.exit(process.returncode)
