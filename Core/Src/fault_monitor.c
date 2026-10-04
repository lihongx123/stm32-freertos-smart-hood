#include "hood_app.h"
#include "watchdog_health.h"
#ifdef STM32F407xx
#include "motor_sim_task.h"
#include "boot_confirm.h"
#endif
extern volatile uint32_t app_dma_events, app_dma_bytes, app_uart_errors;
void FaultTask(void *argument)
{
    (void)argument;
    TickType_t last=xTaskGetTickCount();
    uint32_t previous=0;
    for (;;) {
        app_beat(TASK_FAULT);
        TickType_t now=xTaskGetTickCount();
        uint32_t faults=0;
#ifdef STM32F407xx
        MotorSimMetrics motor = motor_sim_snapshot();
#endif
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
#ifdef STM32F407xx
        if (motor.fault_mask & MOTOR_SIM_STALL) faults|=FAULT_MOTOR_STALL;
        if (motor.fault_mask & MOTOR_SIM_HALL_LOSS) faults|=FAULT_HALL_LOSS;
#endif
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
    WatchdogGate watchdog_gate = {0};
    for (;;) {
        app_beat(TASK_MONITOR);
        TickType_t now=xTaskGetTickCount();
        uint32_t health=0;
#ifdef STM32F407xx
        MotorSimMetrics motor = motor_sim_snapshot();
#endif
        xSemaphoreTake(app_mutex,portMAX_DELAY);
        for (unsigned i=0;i<TASK_MONITOR;++i)
            if (!watchdog_heartbeat_fresh(now, app_runtime.heartbeat[i],
                                          pdMS_TO_TICKS(APP_TASK_TIMEOUT_MS)))
                health|=1U<<i;
#ifdef STM32F407xx
        /* A missing motor simulation task is a local software-health failure. */
        if (now > pdMS_TO_TICKS(APP_TASK_TIMEOUT_MS) &&
            now-motor.heartbeat > pdMS_TO_TICKS(APP_TASK_TIMEOUT_MS))
            health |= 1U << TASK_COUNT;
#endif
        app_runtime.health_mask=health;
        AppRuntime snapshot=app_runtime;
        xSemaphoreGive(app_mutex);
        (void)watchdog_gate_update(&watchdog_gate, health);
#ifdef STM32F407xx
        if (f407_boot_try_confirm((uint32_t)now * portTICK_PERIOD_MS,
                                   health, motor.fault_mask))
            app_log("BOOT_CONFIRM,health_window_ms=%u", F407_BOOT_HEALTH_WINDOW_MS);
#endif
        if (health!=previous) { app_log("HEALTH,stalled=%lu",(unsigned long)health); previous=health; }
        if (iteration++%4==0) {
            app_log("HEARTBEAT seq=%u,tick=%lu",iteration/4+1,(unsigned long)now);
            app_log("RXDMA,events=%lu,bytes=%lu,errors=%lu,fallback=%u",
                (unsigned long)app_dma_events,(unsigned long)app_dma_bytes,
                (unsigned long)app_uart_errors,APP_UART_RX_IT_FALLBACK);
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
#ifdef STM32F407xx
            app_log("MOTOR,SIM,target_rpm=%lu,actual_rpm=%lu,hall_rpm=%lu,hall_edges=%lu,duty_permyriad=%lu,pid_saturation=%lu,fault_mask=%lu,stall_events=%lu,feedback_loss_events=%lu",
                (unsigned long)motor.target_rpm,(unsigned long)motor.actual_rpm,
                (unsigned long)motor.hall_rpm,(unsigned long)motor.hall_edges,
                (unsigned long)motor.duty_permyriad,
                (unsigned long)motor.pid_saturation_count,
                (unsigned long)motor.fault_mask,
                (unsigned long)motor.stall_events,
                (unsigned long)motor.feedback_loss_events);
            app_log("WATCHDOG,SIM,eligible=%lu,skipped=%lu,missing_tasks=%lu",
                (unsigned long)watchdog_gate.eligible_refreshes,
                (unsigned long)watchdog_gate.skipped_refreshes,
                (unsigned long)health);
#endif
        }
        vTaskDelayUntil(&last,pdMS_TO_TICKS(APP_HEALTH_PERIOD_MS));
    }
}
