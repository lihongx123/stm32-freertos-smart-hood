# F407 UART firmware-transfer protocol (local software validation)

This is the protocol milestone record. For the subsequent MCU integration,
copy/activation transaction and current evidence, see
`docs/f407_bootloader_update.md`.

This earlier increment added a HAL/RTOS-independent C receiver state machine in
`bootloader/boot_update.c` and a Python PC sender in
`tools/firmware_updater.py`. They transfer the actual linked F407 application
image through a local pipe-backed serial stand-in. At that point the C
receiver was not linked into the MCU bootloader; the later implementation
and its separate validation are documented above.

## Wire format and flow

Every frame is `SOF(0xA5) | version(1) | type(1) | sequence(u16 LE) |
payload_length(u16 LE) | payload(0..256 bytes) | CRC32(u32 LE)`.
CRC32 covers all bytes from version through payload, not SOF. The reflected
IEEE CRC32 has initial state `0xFFFFFFFF` and final XOR `0xFFFFFFFF`.
Maximum frame size is 267 bytes. The C parser consumes individual bytes and
rejects bad version, length or CRC. CRC detects accidental corruption but
does not authenticate the sender or protect against a malicious image.

The sender uses stop-and-wait:

1. `START` (type 1, sequence 0) sends the 24-byte image header: magic,
   format, firmware version, image byte count, image CRC32, header CRC32.
2. `CHUNK` (type 2) sends consecutive 256-byte blocks numbered 0 upward;
   only the last block may be shorter.
3. `END` (type 3, sequence = chunk count) asks the receiver to verify the
   complete byte count, image CRC32, initial MSP and Thumb reset vector.
4. Each request receives `ACK` (0x80) or `NACK` (0x81) with the matching
   sequence and a two-byte payload: result code, original request type.
   The request type avoids mistaking a delayed `START/0` ACK for a
   `CHUNK/0` ACK. The PC sender retries a timed-out request up to its
   configured limit and stops on NACK.

The C receiver allows a repeated chunk only when its bytes match the
previously staged content; it returns ACK without another write or CRC
update. A repeated END after a lost final ACK is also idempotent. A repeated
START abandons a partial transfer and begins again. Missing/out-of-order
chunks, conflicting duplicates, wrong final CRC/vector, invalid header and
more than 2000 ms between active requests are rejected. START validates a
word-aligned image of 8..262144 bytes; the initial MSP must be 8-byte
aligned in the configured F407 SRAM range and the Thumb reset address must
lie in the active application image.

`BootUpdateSink` separates transport/state from storage. The original local
tests used a byte-array sink. The later STM32 HAL adapter and lifecycle
manager add Staging Flash, read-back, metadata reservation and Pending
commit; the host Flash model remains a software simulation, not physical
power-loss proof.

## Reproduce

From this directory run `scripts/run_f407_local_validation.sh`. The
dedicated checks are `tests/boot_update_test.c` and
`tests/firmware_updater_test.py`. The latter launches the C receiver through
`tests/boot_update_stdio.c` and transfers
`build-f407/SensorTelemetryF407.bin` in normal, lost-ACK and stale-ACK cases.
The test verifies exact image size/CRC, one write per unique chunk, one
finish, and exactly one retransmission in the lost-ACK case. Unit tests
also cover invalid frames, duplicate/missing chunks, restart, timeout,
wrong CRC and invalid vector. `boot_update.c` cross-compiles as a
freestanding Cortex-M4F object.
The consolidated result is
`results/f407-uart-update-software/run-20261004-b/result.json`, with the
complete build, host and Renode regression output in the adjacent
`full-validation.log`.

`tools/firmware_updater.py` also has a pyserial command-line adapter. The
MCU receiver now builds, but its full Flash activation cannot be verified
by the current Renode sector model and has not been run on a physical board.
