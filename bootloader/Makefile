CUBE_F4 ?= $(HOME)/STM32Cube/Repository/STM32CubeF4
CMSIS = $(CUBE_F4)/Drivers/CMSIS
HAL = $(CUBE_F4)/Drivers/STM32F4xx_HAL_Driver
BUILD = build-boot-f407
TARGET = $(BUILD)/HoodBootF407
CPU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
INCLUDES = -Ibootloader -If407/Inc -I$(HAL)/Inc -I$(HAL)/Inc/Legacy \
           -I$(CMSIS)/Device/ST/STM32F4xx/Include -I$(CMSIS)/Include
CFLAGS = $(CPU) -DSTM32F407xx -DUSE_HAL_DRIVER -DBOOT_BOARD_VDD_MV=3300 \
         $(INCLUDES) -std=c11 -Os -g3 -Wall -Wextra \
         -ffunction-sections -fdata-sections -MMD -MP
STARTUP = $(CMSIS)/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s
SYSTEM = $(CMSIS)/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c
OBJS = $(BUILD)/boot_main.o $(BUILD)/boot_image.o $(BUILD)/boot_journal.o \
       $(BUILD)/boot_policy.o $(BUILD)/boot_flash_layout.o \
       $(BUILD)/boot_flash.o $(BUILD)/boot_lifecycle.o \
       $(BUILD)/boot_update.o $(BUILD)/boot_flash_stm32.o \
       $(BUILD)/boot_uart.o $(BUILD)/stm32f4xx_hal.o \
       $(BUILD)/stm32f4xx_hal_cortex.o $(BUILD)/stm32f4xx_hal_rcc.o \
       $(BUILD)/stm32f4xx_hal_flash.o \
       $(BUILD)/stm32f4xx_hal_flash_ex.o $(BUILD)/system_stm32f4xx.o \
       $(BUILD)/startup_stm32f407xx.o

.PHONY: all
all: $(TARGET).elf $(TARGET).bin
	arm-none-eabi-size $(TARGET).elf

$(BUILD):
	mkdir -p $@

$(BUILD)/boot_%.o: bootloader/boot_%.c | $(BUILD)
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

$(BUILD)/stm32f4xx_hal.o: $(HAL)/Src/stm32f4xx_hal.c | $(BUILD)
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

$(BUILD)/stm32f4xx_hal_%.o: $(HAL)/Src/stm32f4xx_hal_%.c | $(BUILD)
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

$(BUILD)/system_stm32f4xx.o: $(SYSTEM) | $(BUILD)
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

$(BUILD)/startup_stm32f407xx.o: $(STARTUP) | $(BUILD)
	arm-none-eabi-gcc $(CPU) -x assembler-with-cpp -c $< -o $@

$(TARGET).elf: $(OBJS) bootloader/STM32F407VGT6_BOOT.ld
	arm-none-eabi-gcc $(OBJS) $(CPU) -specs=nano.specs -specs=nosys.specs \
	  -Tbootloader/STM32F407VGT6_BOOT.ld -Wl,--gc-sections \
	  -Wl,-Map=$(TARGET).map,--cref -Wl,--print-memory-usage \
	  -lc -lnosys -o $@

$(TARGET).bin: $(TARGET).elf
	arm-none-eabi-objcopy -O binary -S $< $@

-include $(OBJS:.o=.d)
