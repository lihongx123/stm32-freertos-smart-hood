#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make -s -j4
make -s f103 -j4
make -s -f bootloader/Makefile -j4

cc -ICore/Inc tests/unit.c Core/Src/hood_state.c Core/Src/uart_ring.c \
  Core/Src/uart_dma_rx.c -o /tmp/hood-f407-host-unit
/tmp/hood-f407-host-unit

cc -ICore/Inc tests/dma_rx_test.c Core/Src/uart_ring.c \
  Core/Src/uart_dma_rx.c -o /tmp/hood-f407-dma-test
/tmp/hood-f407-dma-test

cc -ICore/Inc tests/sensor_acquisition_test.c Core/Src/sensor_acquisition.c \
  -o /tmp/hood-f407-sensor-test
/tmp/hood-f407-sensor-test

cc -ICore/Inc tests/motor_control_test.c Core/Src/motor_control.c -lm \
  -o /tmp/hood-f407-motor-test
/tmp/hood-f407-motor-test

cc -ICore/Inc tests/motor_plant_sim_test.c Core/Src/motor_plant_sim.c \
  Core/Src/motor_control.c -lm -o /tmp/hood-f407-motor-plant-test
/tmp/hood-f407-motor-plant-test

cc -ICore/Inc tests/watchdog_health_test.c Core/Src/watchdog_health.c \
  -o /tmp/hood-f407-watchdog-test
/tmp/hood-f407-watchdog-test

cc -If407/Inc tests/reset_reason_test.c f407/reset_reason.c \
  -o /tmp/hood-f407-reset-test
/tmp/hood-f407-reset-test

cc -std=c11 -Wall -Wextra -Werror -Ibootloader tests/boot_image_test.c \
  bootloader/boot_image.c -o /tmp/hood-f407-boot-image-test
/tmp/hood-f407-boot-image-test build-f407/SensorTelemetryF407.bin
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 \
  -mfloat-abi=hard -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -Ibootloader -c bootloader/boot_image.c -o /tmp/hood-f407-boot-image-arm.o

cc -std=c11 -Wall -Wextra -Werror -Ibootloader tests/boot_update_test.c \
  bootloader/boot_update.c bootloader/boot_image.c \
  -o /tmp/hood-f407-boot-update-test
/tmp/hood-f407-boot-update-test
cc -std=c11 -Wall -Wextra -Werror -Ibootloader tests/boot_update_stdio.c \
  bootloader/boot_update.c bootloader/boot_image.c \
  -o /tmp/hood-f407-boot-update-stdio
cc -std=c11 -Wall -Wextra -Werror -Ibootloader -Itests \
  tests/boot_lifecycle_stdio.c tests/boot_test_flash.c \
  bootloader/boot_flash_layout.c bootloader/boot_flash.c \
  bootloader/boot_lifecycle.c bootloader/boot_update.c \
  bootloader/boot_image.c bootloader/boot_journal.c \
  bootloader/boot_policy.c -o /tmp/hood-f407-lifecycle-stdio
python3 tests/firmware_updater_test.py /tmp/hood-f407-boot-update-stdio \
  build-f407/SensorTelemetryF407.bin /tmp/hood-f407-lifecycle-stdio
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ibootloader -Itests tests/boot_lifecycle_test.c tests/boot_test_flash.c \
  bootloader/boot_flash_layout.c bootloader/boot_flash.c \
  bootloader/boot_lifecycle.c bootloader/boot_update.c \
  bootloader/boot_image.c bootloader/boot_journal.c bootloader/boot_policy.c \
  -o /tmp/hood-f407-lifecycle-test
/tmp/hood-f407-lifecycle-test build-f407/SensorTelemetryF407.bin
cc -std=c11 -Wall -Wextra -Werror -Ibootloader -Itests \
  tests/boot_flash_sector_probe.c tests/boot_test_flash.c \
  bootloader/boot_flash_layout.c -o /tmp/hood-f407-flash-sector-probe
/tmp/hood-f407-flash-sector-probe
cc -std=c11 -Wall -Wextra -Werror -If407/Inc tests/boot_confirm_test.c \
  f407/boot_confirm_policy.c -o /tmp/hood-f407-confirm-test
/tmp/hood-f407-confirm-test
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 \
  -mfloat-abi=hard -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -Ibootloader -c bootloader/boot_update.c -o /tmp/hood-f407-boot-update-arm.o

cc -std=c11 -Wall -Wextra -Werror -Ibootloader tests/boot_journal_test.c \
  bootloader/boot_journal.c bootloader/boot_image.c \
  -o /tmp/hood-f407-boot-journal-test
/tmp/hood-f407-boot-journal-test
cc -std=c11 -Wall -Wextra -Werror -Ibootloader tests/boot_policy_test.c \
  bootloader/boot_policy.c bootloader/boot_journal.c bootloader/boot_image.c \
  -o /tmp/hood-f407-boot-policy-test
/tmp/hood-f407-boot-policy-test
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 \
  -mfloat-abi=hard -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -Ibootloader -c bootloader/boot_journal.c -o /tmp/hood-f407-boot-journal-arm.o
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 \
  -mfloat-abi=hard -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -Ibootloader -c bootloader/boot_policy.c -o /tmp/hood-f407-boot-policy-arm.o

python3 tests/motor_closed_loop.py
python3 tests/boot_rollback_model.py
python3 tests/f407_boot_chain_probe.py
python3 tests/f407_boot_uart_probe.py
python3 tests/f407_boot_confirm_probe.py
python3 tests/f407_renode_sector_direct_probe.py
python3 tests/f407_renode_sector_direct_probe.py --test-model
python3 tests/f407_boot_update_mcu_probe.py --test-model
python3 tests/f407_dma_model_probe.py
python3 tests/f407_motor_task_probe.py
python3 tests/f407_motor_fault_probe.py
python3 tests/f407_watchdog_probe.py
git diff --check
