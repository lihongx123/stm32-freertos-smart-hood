# F407 local software evidence index

All paths below are relative to this repository. These records do not claim
physical-board validation.

| Area | Evidence | Status |
|---|---|---|
| F407 build | `make`; ELF in `build-f407/` | PASS, Cortex-M4F image |
| F103 regression | `make f103`; ELF in `build/` | PASS |
| Ring/state and DMA cursor | `tests/unit.c`, `tests/dma_rx_test.c` | PASS host |
| Sensor backend contract | `tests/sensor_acquisition_test.c` | PASS host |
| F407 USART1→DMA2 short frame | `renode/f407-uart-dma-probe.resc`, `results/f407-renode-dma-probe.log` | PASS Renode with request edge |
| F407 circular DMA 260-byte wrap | `tests/f407_dma_model_probe.py`, `results/f407-dma-model/` | PASS test-only Renode DMA model |
| Motor target/Hall/PID | `tests/motor_control_test.c` | PASS host |
| First-order motor loop | `tests/motor_closed_loop.py`, `results/f407-motor-model/` | PASS host software model |
| Software Hall pulse generation/estimation | `tests/motor_plant_sim_test.c` | PASS host |
| In-firmware sensor→Hall→PID→motor loop | `tests/f407_motor_task_probe.py`, `results/f407-motor-firmware/` | PASS Renode software plant |
| Stall and feedback-loss injection | `tests/f407_motor_fault_probe.py`, `results/f407-motor-fault/` | PASS Renode software fault model |
| Watchdog decision and task stall | `tests/watchdog_health_test.c`, `tests/f407_watchdog_probe.py`, `results/f407-watchdog/` | PASS host/Renode, IWDG not enabled |
| Reset flag decoding | `tests/reset_reason_test.c`, `results/f407-renode-smoke.log` | PASS host decoder; Renode startup record |
| Boot image header/CRC/vector validation | `tests/boot_image_test.c`, `bootloader/boot_image.c` | PASS host, including linked F407 `.bin`; freestanding ARM object compiles |
| Boot metadata journal | `tests/boot_journal_test.c`, `bootloader/boot_journal.c` | PASS host: 16 interrupted word writes, CRC/commit/full guard; freestanding ARM object compiles |
| Boot decision function | `tests/boot_policy_test.c`, `bootloader/boot_policy.c` | PASS host for confirmed/pending/restore/recovery; freestanding ARM object compiles |
| No-RTOS bootloader jump | `bootloader/Makefile`, `tests/f407_boot_chain_probe.py`, `results/f407-boot-chain/` | PASS Renode confirmed-image jump; missing/corrupt record and corrupt app rejected |
| Proposed backup/pending/rollback policy | `tests/boot_rollback_model.py`, `results/f407-boot-policy-model/` | PASS host logical cut-point model; not MCU Flash |
| UART transfer protocol and PC sender | `bootloader/boot_update.c`, `tools/firmware_updater.py`, `tests/boot_update_test.c`, `tests/firmware_updater_test.py`, `results/f407-uart-update-software/` | PASS C host and Python-to-C pipe tests on actual F407 `.bin`; freestanding ARM object compiles |
| Flash layout and lifecycle | `bootloader/boot_flash_layout.*`, `bootloader/boot_lifecycle.*`, `tests/boot_lifecycle_test.c` | PASS host: exact Backup/Active copies, Pending/Confirm/rollback, Staging/activation/rollback fault cuts |
| F407 Flash target backend | `bootloader/boot_flash_stm32.c`, `bootloader/boot_main.c` | IMPLEMENTED, Cortex-M4F build PASS; physical Flash not validated |
| MCU UART recovery and frame rejection | `tests/f407_boot_uart_probe.py`, `results/f407-bootloader-update/` | PASS Renode invalid START NACK, no Flash write |
| MCU Flash activation diagnostic | `tests/f407_boot_update_mcu_probe.py`, `results/f407-bootloader-update/` | NOT VERIFIED in Renode: model leaves `0x08030000` unerased within Sector 5; Active write correctly refused |
| Renode F407 sector probe | `tests/f407_renode_sector_direct_probe.py`, `results/f407-bootloader-update/sector-direct-*`, `results/f407_flash_erase_probe.json` | Stock model: sector 4 erases 16 KiB and sector 5 erases 64 KiB; repository test-only geometry model: sectors 4/5/9 erase fully and adjacent guards remain unchanged |
| MCU activation with test-only F407 flash model | `tests/f407_boot_update_mcu_probe.py --test-model`, `renode/HoodSTM32F407Flash.cs` | PASS software simulation: Active/Backup readback and CRC/lifecycle status; not stock Renode and not physical Flash |
| Application health confirmation | `f407/boot_confirm*.c`, `tests/boot_confirm_test.c`, `tests/f407_boot_confirm_probe.py` | PASS host policy and Renode Pending→Confirmed metadata append; not physically validated |
| I2C/ADC/GPIO sensor hardware backend | none | NOT IMPLEMENTED |
| PWM output and Hall input capture | none | NOT IMPLEMENTED |
| Hardware IWDG/reboot | none | NOT VERIFIED |
| Physical Flash update/rollback and power-loss behavior | none | NOT VERIFIED |

Renode's stock F407 platform lacked the USART1 DMA request edge and its
stock DMA model did not implement circular reload. The repository contains
a narrow, test-only model for DMA2 stream 2; original failed-run logs are
retained alongside subsequent passing runs. See `docs/f407_migration.md`.

Run `scripts/run_f407_local_validation.sh` for the complete build, host-test,
Renode, and whitespace-check sequence. Its tests write new evidence runs under
`results/` and do not overwrite historical runs.
The consolidated bootloader-update run is
`results/f407-bootloader-update/run-20261004-final-1/` (`result.json` and
`full-validation.log`): 3 builds and 23 test commands pass, while full MCU
Flash activation remains **PARTIAL/NOT VERIFIED** in Renode. See
`docs/f407_bootloader_update.md` for the exact boundary.
The image-validation component and its limits are described in
`docs/bootloader.md`; the transfer protocol is in `docs/firmware_update.md`.
