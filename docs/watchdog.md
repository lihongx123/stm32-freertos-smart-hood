# F407 watchdog and reset diagnostics: software validation

The existing Comm, Sensor, Control and Fault task heartbeats are checked by
MonitorTask every 250 ms against a 1500 ms timeout. The F407 simulation adds
its motor task heartbeat to the same missing-task mask. The independent
`watchdog_heartbeat_fresh` function handles a 32-bit tick-counter wrap;
`watchdog_gate_update` counts eligible and skipped refresh decisions.

This is a **software IWDG gate**, not an enabled hardware IWDG. No F407
watchdog peripheral is started or refreshed in this build. The unit test
checks expiration, wraparound and conditional refresh decisions. In Renode,
`T,1,2500` pauses SensorTask: `HEALTH,stalled=2`, skipped refresh decisions
increase, then `HEALTH,stalled=0` appears after the task resumes. Ordinary
sensor or motor process faults do not themselves trigger a watchdog reset
while the tasks remain alive; the IWDG is intended for software liveness.

At F407 startup, the board layer captures raw `RCC->CSR` reset flags once,
clears them, and logs both raw and decoded values over USART1. The decoder
keeps multiple simultaneous causes rather than guessing a single one. Its
bit positions are compile-time checked against the STM32F407 CMSIS header;
the host test covers power-on, external, IWDG and combined flags. Renode's
current boot produced `RESET,raw=0x0E000000,flags=7`; that is simulator
state, **not evidence of a physical power-cycle or watchdog reset**.

Before hardware deployment, the board adapter needs an IWDG timeout chosen
from measured worst-case scheduling latency and a policy for startup/flash
update windows. An end-to-end reset/reboot test requires either a verified
Renode IWDG model or real F407 hardware.
