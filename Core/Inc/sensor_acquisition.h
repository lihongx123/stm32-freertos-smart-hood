#ifndef SENSOR_ACQUISITION_H
#define SENSOR_ACQUISITION_H

#include <stdbool.h>
#include "app_types.h"

/* Read runs in SensorTask context; the backend owns its acquisition timing.
 * A failed read leaves the caller's output unchanged. */
typedef bool (*SensorReadFn)(void *context, SensorData *sample);
typedef struct {
    SensorReadFn read;
    void *context;
} SensorBackend;

bool sensor_acquisition_read(const SensorBackend *backend, SensorData *sample);

#endif
