#ifndef F407_RESET_REASON_H
#define F407_RESET_REASON_H

#include <stdint.h>

enum {
    RESET_REASON_BROWNOUT = 1u << 0,
    RESET_REASON_EXTERNAL = 1u << 1,
    RESET_REASON_POWER_ON = 1u << 2,
    RESET_REASON_SOFTWARE = 1u << 3,
    RESET_REASON_IWDG = 1u << 4,
    RESET_REASON_WWDG = 1u << 5,
    RESET_REASON_LOW_POWER = 1u << 6
};

/* Input is the raw STM32F407 RCC_CSR value. Multiple flags may coexist. */
uint32_t reset_reason_decode_f407(uint32_t csr);
uint32_t board_f407_reset_csr(void);

#endif
