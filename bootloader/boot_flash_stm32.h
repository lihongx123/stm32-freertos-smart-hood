#ifndef BOOT_FLASH_STM32_H
#define BOOT_FLASH_STM32_H

#include "boot_flash.h"

/* STM32F407 internal Flash adapter; not used by host-side flash tests. */
BootFlashBackend boot_flash_stm32_backend(void);
extern volatile uint32_t boot_flash_error_code, boot_flash_error_address;

#endif
