#include "hood_app.h"
void SensorTask(void *argument)
{
    (void)argument;
    TickType_t last=xTaskGetTickCount();
    for (;;) {
        app_beat(TASK_SENSOR);
        xSemaphoreTake(app_mutex,portMAX_DELAY);
        SensorData snapshot=app_runtime.latest;
        xSemaphoreGive(app_mutex);
        if (xQueueSend(sensor_queue,&snapshot,0)!=pdPASS) {
            xSemaphoreTake(app_mutex,portMAX_DELAY);
            ++app_runtime.queue_drop;
            app_runtime.queue_fault_tick=xTaskGetTickCount();
            xSemaphoreGive(app_mutex);
        }
        vTaskDelayUntil(&last,pdMS_TO_TICKS(APP_PERIOD_MS));
    }
}
