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

The STM32 HAL sets `SER`, the sector number in `SNB`, voltage-range `PSIZE`, then `STRT`; the HAL waits for completion and checks Flash status. The adapter also checks `HAL_FLASHEx_Erase` status and the returned failed-sector value before its complete readback. The selected sector number is 5 and the loop covers `0x08020000–0x08040000` with an exclusive end. The first bad word is exactly the boundary between the first and second 64 KiB of sector 5. The adapter does not report success or program Active after that failure.

## Isolated geometry evidence and root cause

The direct register probe `tests/f407_renode_sector_direct_probe.py` fills each whole physical sector with a deterministic pattern, sends the same `FLASH_KEYR` unlock and `FLASH_CR` sector erase register sequence, dumps Flash, scans every byte and checks four-byte guards on both sides. The unchanged stock-model run is `results/f407-bootloader-update/sector-direct-milt70k9/result.json`; the repository test-model run is `results/f407-bootloader-update/sector-direct-qxpra3nj/result.json`.

| Sector | F407 expected range | Stock Renode erase | Stock bad bytes | Test-only model erase | Adjacent guards |
| --- | --- | --- | ---: | --- | --- |
| 4 | `0x08010000–0x08020000` (64 KiB) | first 16 KiB | 49,152 | entire 64 KiB | unchanged |
| 5 | `0x08020000–0x08040000` (128 KiB) | first 64 KiB | 65,536 | entire 128 KiB | unchanged |
| 9 | `0x080A0000–0x080C0000` (128 KiB) | entire 128 KiB | 0 | entire 128 KiB | unchanged |

The [Renode STM32F4 controller source](https://github.com/renode/renode-infrastructure/blob/master/src/Emulator/Peripherals/Peripherals/MTD/STM32F4_FlashController.cs) defines sector 4 as `0x4000` and sector 5 as `0x10000`; the selected STM32F407VGT6 requires `0x10000` and `0x20000`. Its `PerformSectorErase` writes exactly that table's span into the attached memory. This directly explains why `0x08030000` remains programmed. The stock platform's 2 MiB memory mapping is a separate variant mismatch; reducing it to 1 MiB alone cannot repair the wrong sector table.

Primary classification: **D — RENODE_STM32F4_FLASH_MODEL_LIMITATION**. The partition layout and production sector table are aligned and agree with the selected MCU. The full-sector erase check is working as intended.

## Repository-local test model and acceptance

`renode/HoodSTM32F407Flash.cs` derives from Renode's controller for test runs only. It preserves Renode's unlock, register and program behavior and extends only the short sector 4/5 erase spans to F407 geometry. The generated test `.repl` also constrains Flash memory to 1 MiB. The production firmware is built from the unchanged `bootloader/boot_flash_stm32.c` and cannot select this C# model. Direct register probes show zero bad bytes in sectors 4, 5 and 9, with adjacent guards unchanged. The separate host Flash backend checks small and large sector erase, forbidden rewrites, bounds and metadata isolation; `tests/boot_flash_sector_probe.c` passes.

The 64-byte executable-image Renode MCU update with the test model passes at `results/f407-bootloader-update/mcu-update-swssm18m/result.json`: UART transfer, Staging, Active/Backup copy, PENDING status, and old/new marker readback. The unchanged stock model fails at sector 5 and remains recorded. The host lifecycle test exercises the full 38,212-byte linked F407 image, exact copies and CRC, confirmation and rollback, plus interruption points. Renode's short-image success must not be described as physical Flash validation or as a full-size MCU image run.

No physical STM32F407 Flash timing, voltage interruption, endurance or IWDG reset behavior has been validated. The existing host interruption model proves its modeled recovery paths only; it is not a supply-loss guarantee.
