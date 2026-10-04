# STM32F407VGT6 application, linked into the active 256 KiB slot.
CUBE_F4 ?= $(HOME)/STM32Cube/Repository/STM32CubeF4
HAL = $(CUBE_F4)/Drivers/STM32F4xx_HAL_Driver
CMSIS = $(CUBE_F4)/Drivers/CMSIS
FREERTOS = Middlewares/Third_Party/FreeRTOS/Source
BUILD_DIR = build-f407
TARGET = $(BUILD_DIR)/SensorTelemetryF407

CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size
CPU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS = -DUSE_HAL_DRIVER -DSTM32F407xx -DUSER_VECT_TAB_ADDRESS -DVECT_TAB_OFFSET=0x00020000U
INCLUDES = -If407/Inc -ICore/Inc -Ibootloader -I$(HAL)/Inc -I$(HAL)/Inc/Legacy \
           -I$(CMSIS)/Device/ST/STM32F4xx/Include -I$(CMSIS)/Include \
           -I$(FREERTOS)/include -I$(CUBE_F4)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
CFLAGS = $(CPU) $(DEFS) -DBOOT_BOARD_VDD_MV=3300 $(INCLUDES) -std=gnu11 -Og -g3 -Wall \
         -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS = $(CPU) -specs=nano.specs -specs=nosys.specs \
          -Tf407/STM32F407VGT6_APP.ld -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map,--cref -Wl,--print-memory-usage

APP_SOURCES = f407/board_f407.c f407/motor_sim_task.c \
              f407/reset_reason.c f407/boot_confirm.c \
              f407/boot_confirm_policy.c \
              bootloader/boot_image.c bootloader/boot_journal.c \
              bootloader/boot_policy.c bootloader/boot_update.c \
              bootloader/boot_flash_layout.c bootloader/boot_flash.c \
              bootloader/boot_lifecycle.c bootloader/boot_flash_stm32.c \
              Core/Src/hood_app.c Core/Src/comm_task.c Core/Src/uart_ring.c \
              Core/Src/uart_dma_rx.c Core/Src/hood_state.c \
              Core/Src/sensor_task.c Core/Src/control_task.c \
              Core/Src/sensor_acquisition.c \
              Core/Src/motor_control.c \
              Core/Src/motor_plant_sim.c \
              Core/Src/watchdog_health.c \
              Core/Src/fault_monitor.c Core/Src/syscalls.c Core/Src/sysmem.c
HAL_SOURCES = $(addprefix $(HAL)/Src/stm32f4xx_hal_, \
              rcc.c rcc_ex.c gpio.c dma.c cortex.c pwr.c pwr_ex.c \
              flash.c flash_ex.c uart.c)
HAL_SOURCES += $(HAL)/Src/stm32f4xx_hal.c
RTOS_SOURCES = $(addprefix $(FREERTOS)/, \
               list.c queue.c tasks.c timers.c event_groups.c \
               stream_buffer.c portable/MemMang/heap_4.c)
RTOS_SOURCES += $(CUBE_F4)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
SYSTEM_SOURCE = $(CMSIS)/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c
STARTUP = $(CMSIS)/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s
C_SOURCES = $(APP_SOURCES) $(HAL_SOURCES) $(RTOS_SOURCES) $(SYSTEM_SOURCE)
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(BUILD_DIR)/f407_main.o
OBJECTS += $(BUILD_DIR)/startup_stm32f407xx.o
vpath %.c $(sort $(dir $(C_SOURCES)))

.PHONY: all clean
all: $(TARGET).elf $(TARGET).bin
	$(SIZE) $(TARGET).elf

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/f407_main.o: f407/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/startup_stm32f407xx.o: $(STARTUP) | $(BUILD_DIR)
	$(CC) $(CPU) -x assembler-with-cpp -c $< -o $@

$(TARGET).elf: $(OBJECTS) f407/STM32F407VGT6_APP.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -lc -lm -lnosys -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) $(TARGET).elf $(TARGET).bin $(TARGET).map

-include $(OBJECTS:.o=.d)
