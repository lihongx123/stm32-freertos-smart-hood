# STM32F407VGT6 migration audit and execution record

## Baseline before editing (2026-10-03)

The clean repository starts at `b642d37d489445d450c5945ff02c6fdd4c1d28b4`.
The existing target is STM32F103xB/Cortex-M3, built by the root `Makefile` with
`startup_stm32f103xb.s`, `STM32F103xx_FLASH.ld`, STM32F1 HAL and the
FreeRTOS `ARM_CM3` port. `make -j4` passes; `arm-none-eabi-size` reports
text 29388 B, data 108 B, bss 18772 B. The host ring/state test and DMA
cursor/wrap test pass; the latter checks 63112 sequential bytes in 1000
windows. Historical Renode evidence remains under `results/`.

`main.c` uses HSI 8 MHz without PLL, USART1 on PA9/PA10, and a TIM1 HAL
millisecond timebase. `stm32f1xx_hal_msp.c` maps USART1 RX to DMA1 Channel 5;
`stm32f1xx_it.c` dispatches its IRQ. DMA is circular and the application
samples live NDTR in IDLE/HT/TC callbacks, drains only new bytes into a
512-byte software ring, then notifies CommTask. USART/DMA IRQ preemption
priority is 5, matching `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`.
The application has Comm, Sensor, Control, Fault and Monitor tasks, a
4-element SensorData queue, mutex-protected runtime state, and heap_4 with
12288 B configured heap. `SensorTask` currently reads only the UART-fed
`latest` snapshot. State-machine policy is HAL-independent; logging and
task assembly still depend on UART HAL.

The installed STM32CubeF4 package is at
`/home/hello/STM32Cube/Repository/STM32CubeF4`, revision
`5723be54530654c81a1c1ef9da39b2ce1c40564c`. It contains the F407
startup, CMSIS device/system source, STM32F4 HAL and FreeRTOS GCC ARM_CM4F
port. Arm GNU Toolchain 13.2.1 accepts Cortex-M4F. Renode 1.17.0 has an
F4 Cortex-M4 platform, USART1, DMA2, timers, I2C, IWDG and CRC models.
Its stock F4 platform file does not wire an USART1 RX DMA request to DMA2;
the DMA/IDLE path must be tested before any Renode claim. Exact board pinout
is unspecified; any chosen peripheral pins are reference assignments only.

## Migration sequence and gates

1. Keep the F103 target and historical results intact while adding an
   independent F407 build. Use F407 startup, device header, HAL, M4F port,
   linker script and CPU/FPU flags; first gate is clean link and size report.
2. Move UART hardware setup behind a board layer. Configure F407 USART1 RX
   on a valid DMA2 stream/channel, circular byte transfers and IDLE/HT/TC;
   keep the existing cursor/ring/task-notification algorithm. Gate on host
   DMA cursor tests and F407 compilation, then attempt Renode DMA evidence.
3. Introduce sensor input and motor-output interfaces before changing the
   state machine. Add F407 GPIO/I2C/ADC, PWM and Hall reference mappings,
   independent PID with host tests, and a separate motor simulation backend.
4. Add IWDG gating and reset diagnostics, then an independent no-RTOS
   bootloader, sector-aligned image metadata and update/rollback state
   machine. Verify power-loss and corrupt-image transitions at host level.
5. Re-run old behavior tests after each gate; report host, Renode and
   hardware-unverified status separately. Never infer physical timing,
   motor safety or electrical correctness from Renode.

No firmware source had been changed when this audit was written.

## Phase 1/2 execution (2026-10-04)

The independent F407 target now builds with Arm GNU Toolchain 13.2.1,
STM32F4 HAL/CMSIS, the FreeRTOS ARM_CM4F port and hard-float flags. The
application is linked at `0x08020000`; `make -f f407/Makefile -j4` reports
29,584 B text, 108 B data, 16,756 B BSS, 11.35% of its 256 KiB ROM slot
and 12.87% of the 128 KiB main RAM. Two build wiring mistakes were fixed:
the F407 `main.c` receives a unique object name so Make cannot select the
F103 homonym, and unused FreeRTOS static allocation was disabled to match
the application's dynamic task creation. The existing ring/state and DMA
cursor host tests both pass, including 63,112 ordered bytes across 1,000
windows. These are software tests, not a physical F407 validation.

`renode/f407-smoke.resc` boots the F407 ELF on Renode 1.17.0. The UART log
`results/f407-renode-smoke.log` contains `BOOT`, `RTOS_START`, `COMM_READY`
and periodic heartbeats, so CPU start, task scheduling and UART TX work in
this simulation. Renode emits unhandled-register warnings for parts of the
flash cache, RCC, DMA2 and USART error-enable configuration; these were not
silently counted as peripheral validation.

The initial three-byte probe in `renode/f407-uart-dma-probe.resc` sent
`0x41 0x42 0x43` via USART1, but the stock platform reported
`RXDMA,events=1,bytes=0`. That original log is preserved as an earlier
`results/f407-renode-dma-probe.log.N` version. The stock `stm32f4.repl`
declares USART1 and DMA2 but does not connect USART1's `DMARequest` to
DMA2 stream 2. This was a simulator-platform gap, not an MCU diagnosis.

## Local simulator extension and evidence (2026-10-04)

The test-only overlay `renode/f407-dma-edge.repl` connects that request to
DMA2 stream 2. With the stock DMA2 model and the three-byte probe, NDTR
changed from 256 to 253, the first three bytes of the DMA buffer were
`41 42 43`, and the firmware reported `RXDMA,events=1,bytes=3`. The same
script then sent a newline and `S,5,22,45,100,10\n`; the firmware reported
one invalid frame followed by one valid frame (`frame_ok=1,frame_error=1`,
21 bytes total). The stock DMA2 model treats `CIRC` as a tag and cannot
validate wraparound.

For that separate gap, `renode/HoodSTM32F4DMA.cs` is a deliberately narrow
test backend covering DMA2 stream 2 byte receive, NDTR, 256-byte circular
reload, HT/TC flags and interrupt outputs. It does not model the other DMA
streams or all DMA error modes. `tests/f407_dma_model_probe.py` derives an F4
platform at runtime without altering the installed Renode file, sends 260
bytes, checks all 256 DMA buffer bytes and the application receive count.
The verified run at `results/f407-dma-model/run-cof8l4m_/` reported NDTR
`256 -> 252`, expected wrapped memory and
`RXDMA,events=3,bytes=260,errors=0,fallback=0`; PASS for this local
simulation. No real MCU, sensor electronics, motor or bootloader has been
validated by these tests.

## Sensor acquisition boundary (2026-10-04)

`SensorTask` now calls `sensor_acquisition_read` rather than directly
assuming that its sample comes from `app_runtime.latest`. The default
backend still reads the UART simulator snapshot, preserving the existing
application behavior. The abstraction's host test passes, and both F103
and F407 firmware builds pass after the change. The F407 Renode UART frame
test still reports `frame_ok=1,frame_error=1`; the 260-byte circular-DMA
test still passes after the build. The first repeat of that test failed
because the test harness hardcoded the old DMA buffer address; the harness
now resolves `dma_rx` from the ELF symbol table. The failed run remains at
`results/f407-dma-model/run-vkoz9_oe/` rather than being overwritten.

The board-specific I2C/ADC/GPIO/timer acquisition backend is not yet
implemented because no sensor models or pin assignments are provided.
See `docs/sensor_architecture.md` for the contract and remaining work.

The root `make` target now selects F407. The original F103 build remains
available with `make f103` so the historical application can still be
regression-tested without changing its platform files.
After the sensor-interface addition, the current F407 image is 29,728 B
text, 116 B data and 16,756 B BSS; the current F103 regression image is
29,532 B text, 116 B data and 18,772 B BSS. Earlier figures above are the
pre-interface migration snapshot, not the final size.

## Motor-control software components (2026-10-04)

The F407 build now includes a HAL-independent FanMode-to-target adapter,
Hall period/RPM estimator and PID with output/integral clamps and
feedback-loss safe output. Host unit tests pass. The Python first-order
motor model calls the compiled C PID and passes seven local scenarios,
including target steps, load change, feedback loss, recovery and OFF.
This was a control-software test, not PWM/Hall hardware validation. The
integrated software motor task was added next; no PWM/input-capture board
adapter has been wired. See `docs/motor_control.md`.

## Integrated software loop and reliability (2026-10-04)

The F407 image now includes a 20 ms `MotorSimTask` wired to the existing
FanMode state output, C PID and software first-order motor plant. Renode
evidence in `results/f407-motor-firmware/` covers normal sensor-frame input,
target 1400 RPM, convergence near 1388 RPM and duty zero after OFF input.
`results/f407-motor-fault/` records simulation-only stall and Hall-loss
injection, separate fault codes 32 and 64, safe zero duty and recovery.
No PWM or Hall timer peripheral has been driven by these runs.

The heartbeat decision is now a HAL-independent function. The F407 monitor
counts software IWDG-eligible/skipped refresh opportunities; a 2500 ms
SensorTask pause produces a missing-task mask and skipped decisions under
Renode, then recovers. Hardware IWDG is not enabled. F407 startup captures
and clears RCC reset flags and logs the decoded mask; host tests cover
multi-flag decoding. Renode's observed flags are not physical reset proof.

After these additions the F407 size was text 32,188 B, data 116 B,
BSS 20,892 B (ROM 12.34% of the 256 KiB slot, main RAM 16.03% of 128 KiB).
See `docs/watchdog.md` and `docs/EVIDENCE_INDEX.md` for test boundaries.

## Software Hall-feedback refinement (2026-10-04)

The F407 `MotorSimTask` now obtains its PID feedback from timestamped Hall
edges generated by `motor_plant_sim.c` and decoded by the existing C
`HallEstimator`. At the 1400 RPM test point, Renode logged 1390 RPM plant
speed, 1389 RPM Hall estimate and 202 emitted edges before the OFF command;
OFF produced zero duty. Separate stall and Hall-loss injections still
produced faults 32 and 64 with zero duty, followed by recovery. The new
host test checks pulse generation, RPM conversion, missing pulses, recovery
and stalled plant. The full local validation script passes, including the
F103 regression build and prior DMA/watchdog probes.

Current F407 size: text 33,208 B, data 116 B, BSS 20,900 B; ROM use
12.73% of the 256 KiB active slot and RAM use 16.03% of 128 KiB.
This is a simulation of pulse timing and feedback, not TIM input capture
or a physical Hall sensor.

## Boot image validation boundary (2026-10-04)

`bootloader/boot_image.c` now validates a six-field image header, header
and image CRC-32, slot length and alignment, initial MSP and Thumb reset
vector. The host test covers valid, corrupt, truncated, oversized and
invalid-vector images; it also validates the real linked F407 `.bin`.
The same module compiles as a freestanding Cortex-M4F object. This does not
yet create a bootloader ELF or implement UART transfer, Flash programming,
application jump, metadata journal or rollback. See `docs/bootloader.md`.
The separate host-only rollback policy model exercises 34 installation,
11 restoration and 2 confirmation cut points. It is a design test and
does not verify a physical Flash writer or metadata endurance.

The next software increment adds a freestanding 64-byte metadata record
journal with CRC and commit-last programming, plus a pure boot-decision
function. Host tests inject a failure before every record word, verify
retry in a new slot and check recovery decisions. These C modules do not
yet constitute a working MCU update route.

A separate no-RTOS bootloader ELF now links into Sectors 0–3 and performs
a read-only confirmed-image selection. In Renode it reads a committed
Sector-4 record, validates the linked F407 application, sets VTOR/MSP and
jumps successfully. Three negative Renode cases reject missing metadata,
corrupted application bytes and corrupted metadata. Its text/data/BSS size
is 1,872/8/1,056 B. It does not implement UART update, Flash programming
or rollback, so the full productization acceptance criteria remain open.

## UART transfer protocol, still outside the MCU boot path (2026-10-04)

The next increment adds a byte-framed UART update receiver in freestanding C,
a Python PC sender, and a memory-backed local receiver harness. The actual
33,380-byte F407 application binary transfers in 131 chunks with matching
CRC and vector checks. Local tests pass both ordinary transfer and one
deliberately lost ACK: the sender retransmits once while the receiver keeps
one write per unique chunk. A duplicate delayed START ACK is ignored when
waiting for CHUNK 0. Unit tests additionally cover invalid frames,
out-of-order and conflicting chunks, timeout, interrupted transfer restart,
wrong CRC and invalid reset vector. The C receiver cross-compiles as a
freestanding Cortex-M4F object. Full build/regression and Renode results are
indexed in `docs/EVIDENCE_INDEX.md`; protocol details and precise limits are
in `docs/firmware_update.md`.

The MCU bootloader remains unchanged and read-only. Neither a UART recovery
loop nor staging Flash writer is connected to it, so this milestone verifies
protocol software, not on-device firmware replacement.

## Copy-based bootloader update implementation (2026-10-04)

The following increment added a shared sector map, checked Flash interface,
STM32F4 HAL Flash adapter, host-only deterministic 1 MiB Flash model, UART
recovery window, Staging validation, Backup/Active copying, append-only
`BACKUP_READY`/`PENDING` metadata, health-window confirmation and byte-level
rollback. The application remains linked only at `0x08020000`. Host tests
verify exact byte copies and injected interruptions; the F103 regression
build remains independent. Renode still validates the boot/jump route and
UART framing. Its Flash model fails the full Sector-5 erase check at
`0x08030000`, so the MCU activation path is explicitly not recorded as a
Renode PASS. See `docs/f407_bootloader_update.md` and the preserved
`results/f407-bootloader-update/` runs for the exact boundary.
