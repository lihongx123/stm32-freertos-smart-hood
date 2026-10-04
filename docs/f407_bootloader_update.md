# F407 bootloader update lifecycle

## Target and fixed memory map

The configured target and both linker scripts name **STM32F407VGT6** with
1 MiB internal Flash. The address/sector layout below follows ST's
[RM0090 Flash organization](https://www.st.com/resource/en/reference_manual/rm0090-stm32f4xx-reference-manual-stmicroelectronics.pdf).
`bootloader/boot_flash_layout.h` is the shared definition; its compile-time
contiguity assertions and `boot_flash_layout_valid()` check the actual
sector table used by the updater.

| Region | Sectors | Address range (end exclusive) | Capacity |
|---|---:|---|---:|
| Bootloader | 0–3, 16 KiB each | `0x08000000–0x08010000` | 64 KiB |
| Metadata | 4, 64 KiB | `0x08010000–0x08020000` | 64 KiB |
| Active | 5–6, 128 KiB each | `0x08020000–0x08060000` | 256 KiB |
| Backup | 7–8, 128 KiB each | `0x08060000–0x080A0000` | 256 KiB |
| Staging | 9–10, 128 KiB each | `0x080A0000–0x080E0000` | 256 KiB |
| Spare | 11, 128 KiB | `0x080E0000–0x08100000` | 128 KiB |

The application linker still has one fixed execution address,
`0x08020000`; Backup and Staging are *copies*, not directly executable
A/B slots. The maximum accepted image is 262,144 bytes, must be at least
8 bytes and word-aligned, and the image's reset vector must point inside
its actual Active length. The current linked application is smaller than
the slot; exact size is recorded by each validation run.

## Executable flow and storage isolation

On a confirmed-image boot the no-RTOS bootloader opens a 500 ms USART1
polling window (HSI 16 MHz, 115200 baud). It feeds the existing START,
CHUNK and END frames into `BootUpdateSession`. START checks the header
and erases only the Staging sector(s) needed for the declared length;
CHUNK programs aligned 32-bit words with HAL, then reads back each word.
An identical duplicate CHUNK is ACKed without another program operation.
END requires complete ordered bytes, transport/image CRC32, an 8-byte
aligned MSP in the configured F407 SRAM range, and a Thumb reset handler
inside the Active image. The Staging bytes are read back and validated
again before activation. Invalid or incomplete Staging does not touch
Active.

The shared Flash interface has a real STM32F4 HAL adapter and a separate
host-only 1 MiB Flash model. The checked helper accepts image writes only
in Active/Backup/Staging and append-only words in Metadata; normal update
code cannot erase or program Bootloader or Spare. The STM32 adapter uses
`HAL_FLASHEx_Erase` and `HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD)` with
return-code/read-back checks. Its build-time **3.3 V board-supply
assumption** selects `FLASH_VOLTAGE_RANGE_3` (2.7–3.6 V); this is not a
measurement of an actual board. Same-bank erase/program timing and UART
electrical behavior still need physical validation.

After Staging validation, the manager reserves at least eight blank
metadata slots and performs:

1. Erase Backup and copy/validate the old Active image byte-for-byte.
2. Append a committed `BACKUP_READY` record **before** erasing Active.
3. Erase the Active sector(s) covered by the larger old/new image, copy
   Staging into Active and revalidate all declared bytes and vectors.
4. Append a committed `PENDING` record carrying new Active and old Backup
   size/CRC/version. Only then can the new image be selected for boot.

The bootloader replies to END with its existing ACK/NACK and then emits a
`STATUS` frame (`0x82`) after activation. STATUS starts with the lifecycle
result; on the MCU it also includes the failed step and Flash diagnostic
code/address. The Python sender requires STATUS when using a serial port
and accepts a successful STATUS even if the preceding END ACK was lost.
It does not retry a failed activation as though it were an idempotent
chunk. CRC32 detects accidental corruption; it is not sender
authentication or a firmware signature.

## Metadata, confirmation and rollback

Sector 4 contains 64-byte append-only records: magic, format, sequence,
state, Active/Backup/Staging version+length+CRC, attempt count, metadata
CRC and a commit word. The commit word is programmed last. Boot scanning
ignores torn/CRC-invalid records and selects the highest valid sequence.
No metadata sector erase/compaction is implemented; an update is refused
before destructive copying if fewer than eight blank records remain.

`CONFIRMED` boots the validated Active image. `BACKUP_READY` means a
validated Backup exists and Active may have been interrupted; boot either
keeps the intact old Active or restores Backup. `PENDING` records an
attempt *before* each jump. The F407 MonitorTask confirms only after at
least 5,000 ms of uptime, all monitored tasks are fresh and the software
motor model reports no fault. Confirmation appends `CONFIRMED`; it does not
erase Backup. If two pending attempts go unconfirmed, boot copies Backup
back into Active, validates the restored bytes and appends `CONFIRMED`
for the old version. Backup remains available until a later confirmed
update begins reusing that slot.

The boot-to-app jump still disables/clears interrupts, stops SysTick,
relocates VTOR to `0x08020000`, loads the image MSP and branches to the
Thumb Reset_Handler. F103 code remains an independent build target.

## Evidence and limits

The consolidated local run is
`results/f407-bootloader-update/run-20261004-final-1/`:
`result.json` summarizes 3 passing builds and 23 passing test commands;
`full-validation.log` preserves their output. The overall result is
**PARTIAL**, because full MCU Flash activation in Renode is not verified.

The host Flash model enforces erase-before-program, 1-to-0 programming,
word alignment and boundary checks. It can interrupt a sector erase
halfway or a word write after two bytes. Tests compare exact Active,
Backup and Staging bytes, CRC and bootability; they cover confirmed boot,
Pending→Confirmed, two failed pending boots→real Backup restoration,
five Staging interruptions, 15 activation interruptions and one
interrupted rollback restore. The earlier journal test separately cuts
all 16 record-word positions. These are deterministic software faults,
not voltage or endurance tests.

Renode verifies the current boot/jump path and USART1 frame rejection.
A valid START receives ACK and reaches Staging erase in Renode. A further
64-byte MCU update diagnostic receives END ACK but activation reports
`FLASH_ERROR` at `ACTIVE_ERASE`: Renode leaves `0x08030000` unerased,
although that address lies inside F407 Sector 5, which spans
`0x08020000–0x08040000`. The diagnostic explicitly checks the full
sector, refuses to program Active and preserves Backup. The failed
run and Flash-controller warnings are retained under
`results/f407-bootloader-update/`. We do not alter the broad Renode Flash
model or call this a complete MCU/Renode Flash update PASS. The full
transaction semantics are verified by the host model, while MCU
Flash timing, reset behavior, write protection and physical power-loss
safety remain unverified.

The Flash investigation in [f407_flash_erase_investigation.md](f407_flash_erase_investigation.md) subsequently confirmed a Renode sector-table mismatch. A repository-local test-only controller corrects the sector 4/5 erase spans. With this model, an isolated whole-sector probe passes for sectors 4, 5 and 9, and the 64-byte MCU update reaches PENDING with Active and Backup markers read back. The stock Renode failure is retained. The production Flash adapter and its full-sector verification were not changed. The full 38,212-byte linked image remains validated in the host lifecycle model; this short Renode run is not a full-size MCU update or physical Flash test.

Separately, `tests/f407_boot_confirm_probe.py` loads a valid PENDING
record and the linked application, runs the application past the 5-second
health window, observes `BOOT_CONFIRM` and reads a second committed
metadata record with state `CONFIRMED` in Renode. This validates the
application-side software hook in that emulator, not the failed
Staging→Active erase/copy route or any physical Flash guarantee.
