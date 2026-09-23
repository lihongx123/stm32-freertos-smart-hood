#include "hood_app.h"
void ControlTask(void *argument)
{
    (void)argument;
    HoodState state={0};
    SensorData snapshot={0};
    static const char *states[]={"INIT","NORMAL","WARNING","FAULT","RECOVERY"};
    static const char *fans[]={"OFF","LOW","MEDIUM","HIGH","BOOST"};
    app_log("STATE,INIT");
    for (;;) {
        app_beat(TASK_CONTROL);
        xQueueReceive(sensor_queue,&snapshot,pdMS_TO_TICKS(APP_PERIOD_MS));
        xSemaphoreTake(app_mutex,portMAX_DELAY);
        uint32_t faults=app_runtime.faults | (app_runtime.health_mask ? FAULT_TASK : 0);
        xSemaphoreGive(app_mutex);
        HoodState previous=state;
        hood_step(&state,&snapshot,faults);
        xSemaphoreTake(app_mutex,portMAX_DELAY);
        app_runtime.output=state;
        xSemaphoreGive(app_mutex);
        if (previous.state!=state.state) app_log("STATE,%s,tick=%lu",states[state.state],(unsigned long)xTaskGetTickCount());
        if (previous.fan!=state.fan) app_log("FAN,%s",fans[state.fan]);
        if (previous.light_on!=state.light_on) app_log("LIGHT,%s",state.light_on ? "ON" : "OFF");
    }
}
