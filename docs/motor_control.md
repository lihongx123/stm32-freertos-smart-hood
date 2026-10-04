# F407 motor-control software boundary

`Core/Inc/motor_control.h` separates three HAL-independent concepts:

- `MotorTargets` maps the existing high-level `FanMode` to a board-supplied
  target RPM. No production RPM values have been chosen without a motor spec.
- `HallEstimator` converts consecutive microsecond capture timestamps into
  RPM using a configurable pulses-per-revolution value. Unsigned timestamp
  subtraction supports one 32-bit counter wrap; minimum/maximum period and
  no-pulse timeout reject invalid feedback.
- `PidController` converts target/actual RPM into a 0–1 duty command using
  configured Kp/Ki/Kd, output clamp, conditional integration and integral
  clamp. Motor OFF or invalid feedback resets state and commands zero duty.
  A rising saturation counter is available for diagnostics.

The host unit test covers target selection, Hall wrap/timeout, invalid
period, PID saturation, anti-windup and safe output on feedback loss. The
Python first-order plant test in `tests/motor_closed_loop.py` calls the
actual C PID function via a temporary shared library at a 20 ms interval.
It exercises start, LOW→HIGH, HIGH→LOW, load disturbance, simulated stall
with lost feedback, recovery and OFF. The model assumes 3000 RPM maximum
and a 0.30 s time constant solely for software regression; it is not fitted
to a physical fan. Structured runs are in `results/f407-motor-model/`.

The F407 firmware now also runs `MotorSimTask` every 20 ms. It reads the
existing state machine's `FanMode`, maps it through a **simulation-only**
setpoint table (LOW 900, MEDIUM 1400, HIGH 1900, BOOST 2400 RPM), calls the
C PID and advances an in-firmware first-order plant. The software plant
generates timestamped Hall edges at two pulses per revolution; the C
`HallEstimator` converts those edges back into RPM for the next PID step.
The test-only plant uses a 3000 RPM ceiling and 300 ms time constant.
Its metrics are copied
under `app_mutex` and logged as `MOTOR,SIM`. A Renode test keeps an injected
400-unit smoke sample fresh and observes the 1400 RPM target approaching
1390 RPM, with Hall-estimated feedback near 1389 RPM; after an OFF sample,
duty becomes zero. Another Renode test uses
the simulation-only `M,STALL,0/1` and `M,HALL_LOSS,0/1` commands. A stall
must persist 200 ms before it is raised; either active simulated fault
forces duty zero, sets a distinct fault bit and can recover when cleared.

The separate Python host model still feeds RPM directly. The F407 Renode
firmware path now consumes software-generated Hall edges, not the plant's
internal RPM. It does not use a timer input-capture ISR. These tests verify
application software wiring and Hall-estimator behavior only.
There is no physical motor, motor driver, Hall pulses-per-revolution spec,
safe-duty limit or PCB pin assignment in the repository. Therefore PWM and
Hall hardware behavior and control tuning are not validated. A future board
adapter must own PWM safe OFF, timer configuration and bounded ISR capture;
it must pass measured RPM into this module from task context.
