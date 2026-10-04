#include "sensor_acquisition.h"
#include <assert.h>
#include <stdio.h>

typedef struct { SensorData value; unsigned calls; bool fail; } Fixture;
static bool read_fixture(void *context, SensorData *out)
{
    Fixture *fixture = context;
    fixture->calls++;
    if (fixture->fail) return false;
    *out = fixture->value;
    return true;
}

int main(void)
{
    Fixture fixture = {.value = {.smoke = 230, .temperature = 26,
                                   .humidity = 45, .light = 17,
                                   .differential_pressure = 4,
                                   .tick = 123, .valid = 1}};
    SensorBackend backend = {read_fixture, &fixture};
    SensorData output = {.smoke = -1};
    assert(!sensor_acquisition_read(NULL, &output));
    assert(output.smoke == -1);
    assert(sensor_acquisition_read(&backend, &output));
    assert(output.smoke == 230 && output.temperature == 26 &&
           output.humidity == 45 && output.light == 17 &&
           output.differential_pressure == 4 && output.tick == 123 &&
           output.valid == 1 && fixture.calls == 1);
    fixture.fail = true;
    assert(!sensor_acquisition_read(&backend, &output));
    assert(output.smoke == 230 && fixture.calls == 2);
    backend.read = NULL;
    assert(!sensor_acquisition_read(&backend, &output));
    puts("PASS sensor acquisition backend selection/copy/failure contract");
    return 0;
}
