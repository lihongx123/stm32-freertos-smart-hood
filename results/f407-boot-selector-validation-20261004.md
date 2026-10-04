# F407 read-only boot selector validation — 2026-10-04

Command: `scripts/run_f407_local_validation.sh`

Result: PASS. F407 application, F103 regression target and separate
no-FreeRTOS F407 bootloader all built. The full run passed ten host C test
executables, two Python host models and five Renode probes, followed by
`git diff --check`.

Bootloader image: text 1,872 B, data 8 B, BSS 1,056 B; 1,880 B of its
64 KiB Flash region (2.87%). Active application: text 33,208 B, data
116 B, BSS 20,900 B; its raw `.bin` is 33,380 B.

The 64-byte append-only record test interrupted each of 16 word writes,
verified the old record remained selected, retried in a blank slot,
rejected CRC/commit corruption and refused a full journal. The pure boot
policy test covered confirmed, backup-ready, pending attempt, restore and
recovery decisions. The earlier host policy model covered 34 installation,
11 rollback and 2 confirmation cut points.

Renode boot-chain evidence:

- `f407-boot-chain/run-p2chawd4/result.json`
- Confirmed metadata: bootloader checked the 33,380-byte app (CRC-32
  `0x66CE5CA8`), jumped and the app emitted `BOOT`, `RTOS_START`,
  `COMM_READY`.
- Missing metadata: app not started; boot status 2.
- Corrupted app: app not started; boot status 3.
- Corrupted record: app not started; boot status 2.

Other latest full-run evidence: `f407-motor-model/run-ivlxmvz4/`,
`f407-boot-policy-model/run-1ui6vjgr/`, `f407-dma-model/run-bjxo5v0v/`,
`f407-motor-firmware/run-0koucc6u/`, `f407-motor-fault/run-5sprtsbt/`,
`f407-watchdog/run-fvramqzi/`.

The bootloader link emitted newlib-nano `nosys.specs` warnings for unused
`_close`, `_lseek`, `_read` and `_write` stubs; the build exited successfully.
The selector is read-only. UART firmware transfer, hardware Flash writes,
pending-image attempt recording, backup restore and real power-loss
validation are not implemented or claimed here.
