#include "watchdog_health.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(watchdog_heartbeat_fresh(1000, 500, 500));
    assert(!watchdog_heartbeat_fresh(1001, 500, 500));
    assert(watchdog_heartbeat_fresh(4, 0xfffffff0u, 32));
    assert(!watchdog_heartbeat_fresh(40, 0xfffffff0u, 32));
    assert(!watchdog_heartbeat_fresh(0, 0, 0));
    WatchdogGate gate = {0};
    assert(watchdog_gate_update(&gate, 0));
    assert(!watchdog_gate_update(&gate, 1u << 2));
    assert(watchdog_gate_update(&gate, 0));
    assert(gate.eligible_refreshes == 2 && gate.skipped_refreshes == 1);
    assert(!watchdog_gate_update(NULL, 0));
    puts("PASS watchdog heartbeat wrap/expiry and conditional refresh gate");
    return 0;
}
