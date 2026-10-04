#include "boot_confirm.h"

bool f407_boot_should_confirm(uint32_t uptime_ms, uint32_t missing_tasks,
                               uint32_t motor_fault_mask)
{
    return uptime_ms >= F407_BOOT_HEALTH_WINDOW_MS &&
           missing_tasks == 0 && motor_fault_mask == 0;
}
