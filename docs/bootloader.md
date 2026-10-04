# F407 boot image validation — software stage

This page records the earlier image-validation and read-only-selector
milestones. The current update lifecycle and its validation boundary are
documented in `docs/f407_bootloader_update.md`.

The independent `bootloader/boot_image.c` module has no FreeRTOS or HAL
dependency. It compiles as a freestanding Cortex-M4F object and passes
host-side validation of a synthetic image and the currently linked F407
application binary. A separate **read-only** no-FreeRTOS bootloader target
now links at `0x08000000` in the first 64 KiB of Flash.

The transport header contains six little-endian 32-bit fields: magic,
format (1), firmware version, image byte count, image CRC-32 and header
CRC-32. Header CRC covers the first five fields serialized little-endian;
image CRC covers the raw application bytes. The CRC uses the reflected
IEEE polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR
`0xFFFFFFFF`. It detects accidental corruption but does not authenticate
the sender or prevent malicious firmware.

The validator rejects an invalid header, truncated or non-word-aligned
image, image larger than the 256 KiB active slot, CRC mismatch, stack
pointer outside F407 main SRAM/CCM SRAM or not 8-byte aligned, and a reset
handler without the Thumb bit or outside the actual image. The raw image's
first vector-table word is the initial MSP and its second word is the
Thumb Reset_Handler address. The current app linker places that table at
`0x08020000`; its tested initial MSP is `0x20020000` and Reset_Handler is
`0x08027B7D`.

Planned sector-aligned layout for STM32F407VGT6 (1 MiB Flash):

| Sectors | Range (end exclusive) | Planned use |
|---|---|---|
| 0–3 | `0x08000000–0x08010000` | Bootloader, 64 KiB |
| 4 | `0x08010000–0x08020000` | Metadata, 64 KiB |
| 5–6 | `0x08020000–0x08060000` | Active app, 256 KiB |
| 7–8 | `0x08060000–0x080A0000` | Backup, 256 KiB |
| 9–10 | `0x080A0000–0x080E0000` | Staging, 256 KiB |
| 11 | `0x080E0000–0x08100000` | Diagnostics/reserve, 128 KiB |

Sector boundaries match ST's [RM0090 Flash organization table](https://www.st.com/resource/en/reference_manual/rm0090-stm32f4xx-reference-manual-stmicroelectronics.pdf).
The bootloader and active application's linker locations are implemented.
The metadata region is read by the bootloader; backup and staging remain
design allocations without an MCU Flash writer.

The freestanding `boot_journal.c` and `boot_policy.c` modules now express
two more pieces of the proposed boot path. One 64-byte journal record holds
sequence, state, active/backup/staging version, byte count and CRC,
attempt counter, metadata CRC and a commit word. Sector 4 holds at most
1024 records. A record is programmed word by word with its commit word
last; scanning ignores torn or CRC-invalid records and retains the latest
valid sequence. The host callback models Flash's 1-to-0 programming rule.
The C test interrupts each of the 16 word writes, retries in the next blank
slot, corrupts a committed record and verifies that a full journal rejects
an append. The policy module selects boot, record-attempt-before-boot,
backup restore or recovery mode from a validated record and independently
validated active/backup images. Both modules compile as freestanding
Cortex-M4F objects. The read-only selector uses them to inspect Flash
but does not program it.

`tests/boot_rollback_model.py` separately exercises the **proposed logical
policy** with byte-array slots and an append/commit record model. It cuts
power after 34 installation steps, 11 rollback steps and 2 confirmation
steps, and rejects a bad staging CRC. A committed pending image is allowed
two unconfirmed boot attempts before restoring the backup. In this host
model every cut leaves a bootable old or new image; the 33 pre-commit
installation cuts recover the old image, and the final committed-pending
cut boots the new image. This test does **not** simulate STM32 Flash voltage,
word-programming constraints, erase endurance, metadata-sector exhaustion,
or every possible byte-level interruption. It is design evidence only,
not proof that a future MCU implementation is power-loss safe.

## Earlier read-only MCU boot selector milestone

`bootloader/boot_main.c` scans Sector 4, accepts only a committed
`CONFIRMED` record, validates the active image against its recorded length
and CRC, checks its MSP and Thumb Reset_Handler, and applies the boot
decision. Before jumping it disables interrupts, stops SysTick, clears
NVIC enables/pending flags, moves VTOR to `0x08020000`, loads the
application MSP and branches to Reset_Handler. A normal C call would keep
the bootloader stack/vector context and is not a valid reset handoff.
Unknown, pending or corrupt states stay in a recovery wait loop; there is
no UART recovery service yet. The selector does not execute
`BACKUP_READY` or `PENDING` policy actions because these require a Flash
writer and metadata append integration.

`tests/f407_boot_chain_probe.py` loads the bootloader, application and a
host-generated confirmed metadata record in Renode. The application emits
`BOOT`, `RTOS_START` and `COMM_READY` after the jump. Separate Renode
cases reject missing metadata, a corrupted application and a corrupted
record; the bootloader status word identifies the failure. These are
software simulation results, not physical flash/boot measurements.

The bootloader build links newlib-nano startup support and emits warnings
for unused `_close`, `_lseek`, `_read` and `_write` stubs from
`nosys.specs`. It performs no file I/O; these warnings do not affect the
observed boot path.

The later UART, HAL Flash and lifecycle implementation is described in
`docs/f407_bootloader_update.md`. Its complete transaction passes a host
Flash model; the current Renode Flash model does not correctly erase the
full F407 Sector 5, so it does not prove an MCU-side activation PASS.
