"""Host-only power-cut model for the proposed staging/backup boot policy.

This is not an MCU flash driver, UART updater, metadata implementation or
bootloader binary. A cut is injected after each logical chunk/write step.
"""
import copy
import json
import pathlib
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
CHUNK = 128
MAX_ATTEMPTS = 2


class PowerCut(Exception):
    pass


def crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def record_crc(record):
    fields = (record['sequence'], record['state'], record['old_crc'],
              record['new_crc'], record['attempt'])
    return crc('|'.join(map(str, fields)).encode('ascii'))


class FlashModel:
    def __init__(self, old):
        self.old = old
        self.active = old
        self.backup = b''
        self.staging = b''
        self.journal = []
        self.steps = 0
        self.cut_after = None
        self._append('CONFIRMED', crc(old), 0, 0)

    def checkpoint(self):
        self.steps += 1
        if self.steps == self.cut_after:
            raise PowerCut()

    def _append(self, state, old_crc, new_crc, attempt):
        record = dict(sequence=len(self.journal) + 1, state=state,
                      old_crc=old_crc, new_crc=new_crc, attempt=attempt,
                      committed=False)
        record['crc'] = record_crc(record)
        self.journal.append(record)
        self.checkpoint()  # Power loss before commit leaves a torn record.
        record['committed'] = True
        self.checkpoint()

    def latest(self):
        valid = [r for r in self.journal if r['committed'] and
                 r['crc'] == record_crc(r)]
        assert valid
        return valid[-1]

    def _copy(self, destination, image):
        setattr(self, destination, b'')  # Erase destination slot.
        self.checkpoint()
        for end in range(CHUNK, len(image) + CHUNK, CHUNK):
            setattr(self, destination, image[:min(end, len(image))])
            self.checkpoint()

    def install(self, new_image, expected_crc):
        assert len(new_image) <= 256 * 1024
        self._copy('staging', new_image)
        if crc(self.staging) != expected_crc:
            raise ValueError('staging CRC mismatch')
        self.checkpoint()
        old_crc = crc(self.active)
        self._copy('backup', self.active)
        assert crc(self.backup) == old_crc
        self.checkpoint()
        self._append('BACKUP_READY', old_crc, expected_crc, 0)
        self._copy('active', self.staging)
        assert crc(self.active) == expected_crc
        self.checkpoint()
        self._append('PENDING', old_crc, expected_crc, 0)

    def recover(self):
        state = self.latest()
        old_crc, new_crc = state['old_crc'], state['new_crc']
        if state['state'] == 'CONFIRMED':
            assert crc(self.active) == old_crc
            return 'new' if self.active != self.old else 'old'
        assert crc(self.backup) == old_crc
        if state['state'] == 'BACKUP_READY' or crc(self.active) != new_crc or \
                state['attempt'] >= MAX_ATTEMPTS:
            if crc(self.active) != old_crc:
                self._copy('active', self.backup)
            assert crc(self.active) == old_crc
            self._append('CONFIRMED', old_crc, 0, 0)
            return 'old'
        assert state['state'] == 'PENDING'
        self._append('PENDING', old_crc, new_crc, state['attempt'] + 1)
        return 'new'

    def confirm(self):
        state = self.latest()
        assert state['state'] == 'PENDING'
        assert crc(self.active) == state['new_crc']
        self._append('CONFIRMED', state['new_crc'], 0, 0)


old_image = bytes(i % 251 for i in range(1024))
new_image = bytes((i * 3 + 7) % 251 for i in range(1024))
baseline = FlashModel(old_image)
baseline.steps = 0
normal = copy.deepcopy(baseline)
normal.install(new_image, crc(new_image))
install_steps = normal.steps
assert normal.recover() == 'new'
normal.confirm()
assert normal.recover() == 'new'

cut_outcomes = {'old': 0, 'new': 0}
for cut in range(1, install_steps + 1):
    flash = copy.deepcopy(baseline)
    flash.cut_after = cut
    try:
        flash.install(new_image, crc(new_image))
    except PowerCut:
        pass
    else:
        raise AssertionError(f'cut {cut} did not interrupt')
    flash.cut_after = None
    outcome = flash.recover()
    cut_outcomes[outcome] += 1
    assert crc(flash.active) in (crc(old_image), crc(new_image))
    if outcome == 'new':
        assert flash.recover() == 'new'
        assert flash.recover() == 'old'  # No health confirmation: rollback.
        assert flash.active == old_image

bad_crc = copy.deepcopy(baseline)
try:
    bad_crc.install(new_image, crc(new_image) ^ 1)
except ValueError:
    pass
else:
    raise AssertionError('corrupt staging was accepted')
assert bad_crc.recover() == 'old' and bad_crc.active == old_image

rollback_seed = copy.deepcopy(baseline)
rollback_seed.install(new_image, crc(new_image))
assert rollback_seed.recover() == 'new'
confirm_cuts = 0
for cut in (1, 2):
    flash = copy.deepcopy(rollback_seed)
    flash.steps = 0
    flash.cut_after = cut
    try:
        flash.confirm()
    except PowerCut:
        confirm_cuts += 1
        flash.cut_after = None
        assert flash.recover() == 'new'
    else:
        raise AssertionError(f'confirmation cut {cut} did not interrupt')
assert rollback_seed.recover() == 'new'
rollback_cuts = 0
for cut in range(1, 2 + (len(old_image) + CHUNK - 1) // CHUNK + 2):
    flash = copy.deepcopy(rollback_seed)
    flash.steps = 0
    flash.cut_after = cut
    try:
        flash.recover()
    except PowerCut:
        rollback_cuts += 1
        flash.cut_after = None
        assert flash.recover() == 'old'
        assert flash.active == old_image

summary = dict(kind='host-boot-rollback-policy-model',
               install_cut_points=install_steps, cut_outcomes=cut_outcomes,
               interrupted_rollback_cases=rollback_cuts,
               interrupted_confirmation_cases=confirm_cuts,
               bad_staging_crc_rejected=True, pending_attempt_limit=MAX_ATTEMPTS,
               result='PASS', scope='logical state model only')
out = ROOT / 'results/f407-boot-policy-model'
out.mkdir(parents=True, exist_ok=True)
run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=out))
(run / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print('evidence=' + str(run / 'result.json'))
print(json.dumps(summary, indent=2))
