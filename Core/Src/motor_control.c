#include "motor_control.h"
#include <math.h>
#include <string.h>

static float clampf(float value, float lower, float upper)
{
    return value < lower ? lower : (value > upper ? upper : value);
}

uint32_t motor_target_rpm(FanMode mode, const MotorTargets *targets)
{
    if (!targets) return 0;
    switch (mode) {
    case FAN_LOW: return targets->low;
    case FAN_MEDIUM: return targets->medium;
    case FAN_HIGH: return targets->high;
    case FAN_BOOST: return targets->boost;
    default: return 0;
    }
}

bool hall_init(HallEstimator *hall, uint16_t pulses_per_revolution,
               uint32_t min_period_us, uint32_t max_period_us,
               uint32_t timeout_us)
{
    if (!hall || !pulses_per_revolution || !min_period_us ||
        min_period_us > max_period_us || !timeout_us) return false;
    memset(hall, 0, sizeof(*hall));
    hall->pulses_per_revolution = pulses_per_revolution;
    hall->min_period_us = min_period_us;
    hall->max_period_us = max_period_us;
    hall->timeout_us = timeout_us;
    return true;
}

bool hall_on_edge(HallEstimator *hall, uint32_t timestamp_us)
{
    if (!hall || !hall->pulses_per_revolution) return false;
    if (!hall->seen_edge) {
        hall->seen_edge = true;
        hall->last_edge_us = timestamp_us;
        return false;
    }
    /* Unsigned subtraction handles one wrap of a 32-bit microsecond clock. */
    uint32_t period = timestamp_us - hall->last_edge_us;
    hall->last_edge_us = timestamp_us;
    hall->period_valid = period >= hall->min_period_us &&
                         period <= hall->max_period_us;
    if (hall->period_valid) hall->period_us = period;
    return hall->period_valid;
}

bool hall_read_rpm(const HallEstimator *hall, uint32_t now_us, uint32_t *rpm)
{
    if (!hall || !rpm || !hall->seen_edge || !hall->period_valid ||
        now_us - hall->last_edge_us > hall->timeout_us) return false;
    uint64_t denominator = (uint64_t)hall->period_us * hall->pulses_per_revolution;
    *rpm = (uint32_t)(60000000ULL / denominator);
    return true;
}

bool hall_feedback_lost(const HallEstimator *hall, uint32_t now_us,
                        uint32_t command_start_us, bool motor_commanded)
{
    if (!hall || !motor_commanded) return false;
    /* Invalid/noisy edges must not indefinitely postpone a no-feedback fault. */
    uint32_t reference = hall->period_valid ? hall->last_edge_us : command_start_us;
    return now_us - reference > hall->timeout_us;
}

bool pid_init(PidController *pid, const PidConfig *config)
{
    if (!pid || !config || !isfinite(config->kp) || !isfinite(config->ki) ||
        !isfinite(config->kd) || config->kp < 0 || config->ki < 0 ||
        config->kd < 0 || !isfinite(config->integral_min) ||
        !isfinite(config->integral_max) || !isfinite(config->output_min) ||
        !isfinite(config->output_max) ||
        config->integral_min > config->integral_max ||
        config->output_min < 0 || config->output_min >= config->output_max)
        return false;
    memset(pid, 0, sizeof(*pid));
    pid->config = *config;
    return true;
}

void pid_reset(PidController *pid)
{
    if (!pid) return;
    pid->integral = 0;
    pid->previous_error = 0;
    pid->has_previous = false;
}

float pid_update(PidController *pid, float target_rpm, float measured_rpm,
                 float dt_seconds, bool feedback_valid)
{
    if (!pid) return 0;
    if (!feedback_valid || !isfinite(target_rpm) || !isfinite(measured_rpm) ||
        !isfinite(dt_seconds) || target_rpm <= 0 || measured_rpm < 0 ||
        dt_seconds <= 0) {
        pid_reset(pid);
        return 0;
    }
    const float error = target_rpm - measured_rpm;
    const float derivative = pid->has_previous ?
        (error - pid->previous_error) / dt_seconds : 0;
    const float proposed_integral = clampf(pid->integral + error * dt_seconds,
        pid->config.integral_min, pid->config.integral_max);
    float raw = pid->config.kp * error +
                pid->config.ki * proposed_integral + pid->config.kd * derivative;
    if (!((raw > pid->config.output_max && error > 0) ||
          (raw < pid->config.output_min && error < 0)))
        pid->integral = proposed_integral;
    raw = pid->config.kp * error + pid->config.ki * pid->integral +
          pid->config.kd * derivative;
    if (raw > pid->config.output_max || raw < pid->config.output_min)
        pid->saturation_count++;
    pid->previous_error = error;
    pid->has_previous = true;
    return clampf(raw, pid->config.output_min, pid->config.output_max);
}
