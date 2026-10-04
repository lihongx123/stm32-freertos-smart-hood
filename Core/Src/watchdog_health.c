#include "watchdog_health.h"

bool watchdog_heartbeat_fresh(uint32_t now, uint32_t last,
                              uint32_t timeout_ticks)
{
    return timeout_ticks != 0 && now - last <= timeout_ticks;
}

bool watchdog_gate_update(WatchdogGate *gate, uint32_t missing_task_mask)
{
    if (!gate) return false;
    if (missing_task_mask) {
        gate->skipped_refreshes++;
        return false;
    }
    gate->eligible_refreshes++;
    return true;
}
