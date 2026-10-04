#ifndef F407_BOOT_CONFIRM_H
#define F407_BOOT_CONFIRM_H

#include <stdbool.h>
#include <stdint.h>

enum { F407_BOOT_HEALTH_WINDOW_MS = 5000u };

bool f407_boot_should_confirm(uint32_t uptime_ms, uint32_t missing_tasks,
                               uint32_t motor_fault_mask);
bool f407_boot_try_confirm(uint32_t uptime_ms, uint32_t missing_tasks,
                            uint32_t motor_fault_mask);

#endif
