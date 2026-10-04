#ifndef WATCHDOG_HEALTH_H
#define WATCHDOG_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

/* Unsigned subtraction also handles a single 32-bit tick-counter wrap. */
bool watchdog_heartbeat_fresh(uint32_t now, uint32_t last,
                              uint32_t timeout_ticks);

typedef struct {
    uint32_t eligible_refreshes;
    uint32_t skipped_refreshes;
} WatchdogGate;

/* Software gate only; a board driver may refresh IWDG when this returns true. */
bool watchdog_gate_update(WatchdogGate *gate, uint32_t missing_task_mask);

#endif
