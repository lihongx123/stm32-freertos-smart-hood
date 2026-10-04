#include "hood_app.h"

static bool read_uart_simulation(void *context, SensorData *sample)
{
    (void)context;
    xSemaphoreTake(app_mutex, portMAX_DELAY);
    *sample = app_runtime.latest;
    xSemaphoreGive(app_mutex);
    return true;
}

static SensorBackend active_backend = { read_uart_simulation, NULL };

void sensor_task_set_backend(SensorBackend backend)
{
    configASSERT(xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED);
    configASSERT(backend.read);
    active_backend = backend;
}

void SensorTask(void *argument)
{
    (void)argument;
    TickType_t last=xTaskGetTickCount();
    for (;;) {
        app_beat(TASK_SENSOR);
        SensorData snapshot = {0};
        if (!sensor_acquisition_read(&active_backend, &snapshot)) snapshot.valid = 0;
        if (xQueueSend(sensor_queue,&snapshot,0)!=pdPASS) {
            xSemaphoreTake(app_mutex,portMAX_DELAY);
            ++app_runtime.queue_drop;
            app_runtime.queue_fault_tick=xTaskGetTickCount();
            xSemaphoreGive(app_mutex);
        }
        vTaskDelayUntil(&last,pdMS_TO_TICKS(APP_PERIOD_MS));
    }
}
