# STM32F407 Flash erase investigation

## Unmodified baseline reproduction (2026-10-04)

- HEAD: `b642d37d489445d450c5945ff02c6fdd4c1d28b4`; worktree already dirty with F407 migration, bootloader, tests and evidence. No source was changed before reproduction.
- MCU: STM32F407VGT6, 1 MiB internal Flash. Renode v1.17.0 (`1.17.0+20260922gitd6193cf47`).
- Exact command: `python3 tests/f407_boot_update_mcu_probe.py` from the SensorTelemetry root. Fresh output: `results/f407-bootloader-update/mcu-update-maljtg2j/`.
- Result: `NOT_VERIFIED`; activation step 5, Flash error 3, first observed non-erased word `0x08030000`. Expected sector 5 is `0x08020000–0x08040000` (end exclusive), 131072 bytes. Exact bad-byte count and actual erase range require a full-sector dump/probe; the baseline diagnostic only checks until the first bad word.
- Renode script generated in that run loads `platforms/cpus/stm32f4.repl`, preloads a 64-byte Active image and confirmed metadata, loads `HoodBootF407.elf`, sends USART1 update frames and reads back selected words. The platform maps 2 MiB generic memory at `0x08000000`, larger than the selected 1 MiB MCU, and attaches `MTD.STM32F4_FlashController` to that memory. These are facts about the emulator definition, not yet proof of the cause.

## Flash geometry and partitions

The production sector table in `bootloader/boot_flash_layout.c` uses non-uniform F407 sectors. Linker `bootloader/STM32F407VGT6_BOOT.ld` has ROM `0x08000000`/64 KiB and RAM `0x20000000`/128 KiB. Linker `f407/STM32F407VGT6_APP.ld` has ROM `0x08020000`/256 KiB, RAM `0x20000000`/128 KiB and CCMRAM `0x10000000`/64 KiB.

| Region | Start | End exclusive | Sectors | Alignment/overlap |
| --- | --- | --- | --- | --- |
| Bootloader | `0x08000000` | `0x08010000` | 0–3, 16 KiB each | sector-aligned, isolated |
| Metadata | `0x08010000` | `0x08020000` | 4, 64 KiB | sector-aligned, isolated |
| Active | `0x08020000` | `0x08060000` | 5–6, 128 KiB each | sector-aligned, isolated |
| Backup | `0x08060000` | `0x080A0000` | 7–8, 128 KiB each | sector-aligned, isolated |
| Staging | `0x080A0000` | `0x080E0000` | 9–10, 128 KiB each | sector-aligned, isolated |
| Spare | `0x080E0000` | `0x08100000` | 11, 128 KiB | sector-aligned, isolated |

No logical live regions share a physical erase sector. The app execution slot is Active; Backup and Staging hold copies.

## Production erase path and preliminary classification

`BootUpdateSession`/activation use the checked Flash backend. `bootloader/boot_flash_stm32.c` rejects sector numbers outside 5–10, obtains exact bounds from the F407 table, unlocks, calls `HAL_FLASHEx_Erase` for one selected sector at voltage range 3, locks, checks HAL status and then reads **every word of the physical sector** for `0xFFFFFFFF`. Error 3 records the first bad word; the complete verification must remain. The current evidence does not support weakening it or changing the partition map.

Primary classification is **E — UNKNOWN_REQUIRES_MORE_EVIDENCE** pending a one-sector-at-a-time deterministic erase probe and inspection of the actual Renode controller behavior. The 2 MiB platform mapping is a configuration mismatch but has not been shown to cause the sector-5 partial erase.
