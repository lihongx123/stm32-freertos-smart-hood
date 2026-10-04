"""End-to-end local Python sender / C receiver protocol check."""
import json
import pathlib
import select
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import firmware_updater as updater  # noqa: E402


class PipeTransport:
    def __init__(self, process):
        self.process = process

    def write(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()
        return len(data)

    def read(self, count):
        if not select.select([self.process.stdout], [], [], 0.01)[0]:
            return b''
        return self.process.stdout.read(count)


def run_case(receiver, image, drop_ack, stale_ack=False):
    command = [str(receiver)]
    if drop_ack:
        command.append('--drop-chunk-ack')
    if stale_ack:
        command.append('--stale-start-ack')
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               bufsize=0)
    try:
        result = updater.send_update(PipeTransport(process), image, 42,
                                     timeout=0.15, retries=2)
        process.stdin.close()
        error = process.stderr.read().decode()
        assert process.wait(timeout=5) == 0, error
        receiver_result = json.loads(error)
        assert receiver_result['complete']
        assert receiver_result['bytes'] == len(image)
        assert receiver_result['crc32'] == int(result['image_crc32'], 16)
        assert receiver_result['writes'] == result['chunks']
        assert receiver_result['finishes'] == 1
        assert receiver_result['dropped_acks'] == int(drop_ack)
        assert result['retransmissions'] == int(drop_ack)
        return result
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdout.close()
        process.stderr.close()
        if not process.stdin.closed:
            process.stdin.close()


def run_lifecycle_case(receiver, binary, drop_end_ack):
    command = [str(receiver), str(binary)]
    if drop_end_ack:
        command.append('--drop-end-ack')
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               bufsize=0)
    try:
        image = binary.read_bytes()
        result = updater.send_update(PipeTransport(process), image, 2,
                                     timeout=0.15, retries=2,
                                     expect_status=True, status_timeout=5)
        process.stdin.close()
        error = process.stderr.read().decode()
        assert process.wait(timeout=5) == 0, error
        state = json.loads(error)
        assert result['activated'] and state['pending']
        assert state['backup_exact'] and state['active_valid']
        assert state['bytes'] == len(image)
        assert state['crc32'] == int(result['image_crc32'], 16)
        return result
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdout.close()
        process.stderr.close()
        if not process.stdin.closed:
            process.stdin.close()


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit('usage: firmware_updater_test.py RECEIVER APP.bin '
                         '[LIFECYCLE_RECEIVER]')
    receiver, binary = map(pathlib.Path, sys.argv[1:3])
    image = binary.read_bytes()
    updater.validate_image(image)
    expected = len(image) // updater.CHUNK_BYTES
    normal = run_case(receiver, image, False)
    retry = run_case(receiver, image, True)
    stale = run_case(receiver, image, False, True)
    assert normal['chunks'] >= expected
    assert stale['retransmissions'] == 0
    if len(sys.argv) == 4:
        lifecycle = pathlib.Path(sys.argv[3])
        assert run_lifecycle_case(lifecycle, binary, False)['activated']
        assert run_lifecycle_case(lifecycle, binary, True)['activated']
    try:
        updater.validate_image(image[:4])
    except ValueError:
        pass
    else:
        raise AssertionError('truncated image accepted')
    print('PASS Python sender / C receiver: image_bytes={}, chunks={}, '
          'normal_retries=0, dropped_ACK_retries={}, stale_ACK_ignored=1, '
          'lifecycle_status_cases={}'.format(
              len(image), normal['chunks'], retry['retransmissions'],
              2 if len(sys.argv) == 4 else 0))


if __name__ == '__main__':
    main()
