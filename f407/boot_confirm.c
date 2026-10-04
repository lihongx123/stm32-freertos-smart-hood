#include "boot_confirm.h"
#include "boot_lifecycle.h"
#include "boot_flash_stm32.h"

bool f407_boot_try_confirm(uint32_t uptime_ms, uint32_t missing_tasks,
                            uint32_t motor_fault_mask)
{
    static bool confirmed;
    static uint32_t last_attempt_ms;
    if (confirmed || !f407_boot_should_confirm(uptime_ms, missing_tasks,
                                                motor_fault_mask) ||
        (last_attempt_ms && uptime_ms - last_attempt_ms < 1000u))
        return false;
    last_attempt_ms = uptime_ms;
    BootLifecycle life;
    boot_lifecycle_init(&life, boot_flash_stm32_backend());
    if (!life.has_record || life.latest.state != BOOT_STATE_PENDING)
        return false;
    if (boot_lifecycle_confirm(&life) != BOOT_LIFECYCLE_OK)
        return false;
    confirmed = true;
    return true;
}
