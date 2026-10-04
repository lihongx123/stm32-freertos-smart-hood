#include "sensor_acquisition.h"

bool sensor_acquisition_read(const SensorBackend *backend, SensorData *sample)
{
    if (!backend || !backend->read || !sample) return false;
    SensorData candidate = {0};
    if (!backend->read(backend->context, &candidate)) return false;
    *sample = candidate;
    return true;
}
