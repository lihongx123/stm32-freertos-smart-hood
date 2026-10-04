# F407 sensor acquisition: current implementation and board contract

## Data path now

`SensorTask` calls the HAL-independent `SensorBackend.read` interface in
`Core/Inc/sensor_acquisition.h` once per 100 ms period. The default backend
copies the latest UART simulation sample under `app_mutex`, then sends that
`SensorData` to the existing four-element queue. `ControlTask` and the
state machine are unchanged. A backend can be bound before the scheduler
starts; changing it while tasks run is intentionally unsupported.

`sensor_acquisition_read` copies a complete sample only on success. On a
failed read, `SensorTask` sends an invalid sample rather than reusing an
uninitialized one. The host test `tests/sensor_acquisition_test.c` covers
selection, full-field copy, failure leaving the output unchanged, and
missing callback handling. The UART simulation path was retested under
F407 Renode with one valid sensor frame after the abstraction change.

## What a physical-board backend must provide

The repository has no specified sensor part numbers, transfer functions,
I2C addresses, analog ranges, or PCB pinout. A board backend must provide a
`SensorReadFn` that acquires calibrated smoke, temperature, humidity, light
and differential-pressure values and sets `SensorData.valid` and `tick`.
It may use I2C, ADC, GPIO and timer measurements behind that callback;
those buses must not be assumed to map one-to-one to a physical sensor.
The backend must own its HAL initialization, conversions, timeout and error
handling. It must not call a FreeRTOS mutex from an ISR.

The current board backend is **not implemented**. Merely compiling the
abstraction does not validate I2C/ADC/GPIO, sensor calibration, or board
electrical behavior. When a physical backend is enabled, `FaultTask` must
also distinguish hardware freshness from the UART simulator's
`COMM_TIMEOUT`; the existing UART-source fault behavior is retained until
that integration is complete.

## Ownership and synchronization

UART DMA ISR -> software ring buffer -> CommTask updates `latest` under
`app_mutex`. The default sensor backend reads it under the same mutex.
SensorTask owns the producer side of `sensor_queue`; ControlTask consumes
copies of `SensorData`. Backend selection is configured only before
`vTaskStartScheduler`, so its function pointer needs no runtime mutex.
