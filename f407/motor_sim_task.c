#include "motor_sim_task.h"
#include "hood_app.h"
#include "motor_control.h"
#include "motor_plant_sim.h"

/* Reference values for the software plant only; not calibrated to a motor. */
static const MotorTargets sim_targets = {900, 1400, 1900, 2400};
static const PidConfig sim_pid = {0.00035f, 0.0005f, 0.0f,
                                  -2000.0f, 2000.0f, 0.0f, 1.0f};
static MotorSimMetrics metrics;
static uint32_t injected_faults;

void motor_sim_set_fault(uint32_t mask, bool enabled)
{
    xSemaphoreTake(app_mutex, portMAX_DELAY);
    if (enabled) injected_faults |= mask;
    else injected_faults &= ~mask;
    xSemaphoreGive(app_mutex);
}

MotorSimMetrics motor_sim_snapshot(void)
{
    xSemaphoreTake(app_mutex, portMAX_DELAY);
    MotorSimMetrics result = metrics;
    xSemaphoreGive(app_mutex);
    return result;
}

void MotorSimTask(void *argument)
{
    (void)argument;
    PidController pid;
    configASSERT(pid_init(&pid, &sim_pid));
    MotorPlantSim plant;
    HallEstimator hall;
    configASSERT(motor_plant_sim_init(&plant, 3000.0f, 0.30f));
    configASSERT(hall_init(&hall, 2, 1000, 100000, 300000));
    TickType_t last = xTaskGetTickCount();
    TickType_t stall_since = 0;
    bool stall_was_requested = false, loss_was_requested = false;
    bool was_commanded = false;
    uint32_t command_start_us = 0;
    uint32_t stall_events = 0, feedback_loss_events = 0;
    app_log("MOTOR_SIM_READY");
    for (;;) {
        xSemaphoreTake(app_mutex, portMAX_DELAY);
        FanMode fan = app_runtime.output.fan;
        uint32_t requested_faults = injected_faults;
        xSemaphoreGive(app_mutex);

        uint32_t target = motor_target_rpm(fan, &sim_targets);
        bool stalled = target && (requested_faults & MOTOR_SIM_STALL);
        bool hall_lost = target && (requested_faults & MOTOR_SIM_HALL_LOSS);
        if ((target && !was_commanded) ||
            (!stalled && stall_was_requested) ||
            (!hall_lost && loss_was_requested)) {
            configASSERT(hall_init(&hall, 2, 1000, 100000, 300000));
            command_start_us = plant.clock_us;
        }
        was_commanded = target != 0;
        TickType_t now = xTaskGetTickCount();
        if (stalled && !stall_was_requested) {
            stall_since = now;
            stall_events++;
        }
        if (hall_lost && !loss_was_requested) feedback_loss_events++;
        stall_was_requested = stalled;
        loss_was_requested = hall_lost;
        bool stall_detected = stalled &&
            now - stall_since >= pdMS_TO_TICKS(200);
        uint32_t measured_rpm = 0;
        if (!hall_read_rpm(&hall, plant.clock_us, &measured_rpm))
            measured_rpm = 0;
        bool feedback_timed_out = hall_feedback_lost(
            &hall, plant.clock_us, command_start_us, target != 0);
        uint32_t active_faults =
            ((stall_detected || (feedback_timed_out && stalled)) ?
                MOTOR_SIM_STALL : 0u) |
            ((hall_lost || (feedback_timed_out && !stalled)) ?
                MOTOR_SIM_HALL_LOSS : 0u);
        float duty = pid_update(&pid, (float)target, (float)measured_rpm, 0.02f,
                                active_faults == 0);
        (void)motor_plant_sim_step(&plant, &hall, duty, 20000, stalled,
                                   !hall_lost);
        if (!hall_read_rpm(&hall, plant.clock_us, &measured_rpm))
            measured_rpm = 0;

        xSemaphoreTake(app_mutex, portMAX_DELAY);
        metrics.target_rpm = target;
        metrics.actual_rpm = (uint32_t)(plant.rpm + 0.5f);
        metrics.hall_rpm = measured_rpm;
        metrics.hall_edges = plant.emitted_edges;
        metrics.duty_permyriad = (uint32_t)(duty * 10000.0f + 0.5f);
        metrics.pid_saturation_count = pid.saturation_count;
        metrics.fault_mask = active_faults;
        metrics.stall_events = stall_events;
        metrics.feedback_loss_events = feedback_loss_events;
        metrics.heartbeat = now;
        xSemaphoreGive(app_mutex);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
    }
}
