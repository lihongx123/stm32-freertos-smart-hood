#include "hood_app.h"
void FaultTask(void *argument)
{
    (void)argument;
    TickType_t last=xTaskGetTickCount();
    uint32_t previous=0;
    for (;;) {
        app_beat(TASK_FAULT);
        TickType_t now=xTaskGetTickCount();
        uint32_t faults=0;
        xSemaphoreTake(app_mutex,portMAX_DELAY);
        if (app_runtime.invalid) faults|=FAULT_INVALID;
        if (now>pdMS_TO_TICKS(APP_INPUT_TIMEOUT_MS)) {
            if (!app_runtime.latest.valid || now-app_runtime.latest.tick>pdMS_TO_TICKS(APP_INPUT_TIMEOUT_MS))
                faults|=FAULT_SENSOR_TIMEOUT;
            if (now-app_runtime.last_frame>pdMS_TO_TICKS(APP_INPUT_TIMEOUT_MS)) faults|=FAULT_COMM_TIMEOUT;
        }
        if (app_runtime.queue_drop && now-app_runtime.queue_fault_tick<pdMS_TO_TICKS(APP_INPUT_TIMEOUT_MS))
            faults|=FAULT_QUEUE;
        if (app_runtime.health_mask) faults|=FAULT_TASK;
        app_runtime.faults=faults;
        xSemaphoreGive(app_mutex);
        if (faults!=previous) { app_log("FAULT,mask=%lu",(unsigned long)faults); previous=faults; }
        vTaskDelayUntil(&last,pdMS_TO_TICKS(APP_PERIOD_MS));
    }
}
void MonitorTask(void *argument)
{
    (void)argument;
    app_tasks[TASK_MONITOR]=xTaskGetCurrentTaskHandle();
    vTaskPrioritySet(NULL,1);
    TickType_t last=xTaskGetTickCount();
    unsigned iteration=0;
    uint32_t previous=0;
    for (;;) {
        app_beat(TASK_MONITOR);
        TickType_t now=xTaskGetTickCount();
        uint32_t health=0;
        xSemaphoreTake(app_mutex,portMAX_DELAY);
        for (unsigned i=0;i<TASK_MONITOR;++i)
            if (now-app_runtime.heartbeat[i]>pdMS_TO_TICKS(APP_TASK_TIMEOUT_MS)) health|=1U<<i;
        app_runtime.health_mask=health;
        AppRuntime snapshot=app_runtime;
        xSemaphoreGive(app_mutex);
        if (health!=previous) { app_log("HEALTH,stalled=%lu",(unsigned long)health); previous=health; }
        if (iteration++%4==0) {
            app_log("HEARTBEAT seq=%u,tick=%lu",iteration/4+1,(unsigned long)now);
            app_log("METRIC,uart_rx_bytes=%lu,frame_ok=%lu,frame_error=%lu,ring_overflow=%lu,queue_drop=%lu",
                (unsigned long)app_ring.bytes_received,(unsigned long)snapshot.frame_ok,
                (unsigned long)snapshot.frame_error,(unsigned long)app_ring.overflow,(unsigned long)snapshot.queue_drop);
            app_log("MEM,free_heap=%lu,stack_comm=%lu,stack_sensor=%lu,stack_control=%lu,stack_fault=%lu,stack_monitor=%lu",
                (unsigned long)xPortGetFreeHeapSize(),(unsigned long)uxTaskGetStackHighWaterMark(app_tasks[0]),
                (unsigned long)uxTaskGetStackHighWaterMark(app_tasks[1]),(unsigned long)uxTaskGetStackHighWaterMark(app_tasks[2]),
                (unsigned long)uxTaskGetStackHighWaterMark(app_tasks[3]),(unsigned long)uxTaskGetStackHighWaterMark(NULL));
            app_log("STATUS,state=%u,fan=%u,light=%u,state_transition_count=%lu,fault_count=%lu,fault_mask=%lu",
                snapshot.output.state,snapshot.output.fan,snapshot.output.light_on,
                (unsigned long)snapshot.output.transitions,(unsigned long)snapshot.output.faults,(unsigned long)snapshot.faults);
        }
        vTaskDelayUntil(&last,pdMS_TO_TICKS(APP_HEALTH_PERIOD_MS));
    }
}
