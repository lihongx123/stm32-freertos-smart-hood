#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "app_types.h"

/* Board/motor-specific RPM setpoints are supplied by configuration. */
typedef struct {
    uint32_t low, medium, high, boost;
} MotorTargets;
uint32_t motor_target_rpm(FanMode mode, const MotorTargets *targets);

typedef struct {
    uint32_t last_edge_us, period_us;
    uint32_t min_period_us, max_period_us, timeout_us;
    uint16_t pulses_per_revolution;
    bool seen_edge, period_valid;
} HallEstimator;
bool hall_init(HallEstimator *hall, uint16_t pulses_per_revolution,
               uint32_t min_period_us, uint32_t max_period_us,
               uint32_t timeout_us);
bool hall_on_edge(HallEstimator *hall, uint32_t timestamp_us);
bool hall_read_rpm(const HallEstimator *hall, uint32_t now_us, uint32_t *rpm);
bool hall_feedback_lost(const HallEstimator *hall, uint32_t now_us,
                        uint32_t command_start_us, bool motor_commanded);

typedef struct {
    float kp, ki, kd;
    float integral_min, integral_max;
    float output_min, output_max;
} PidConfig;
typedef struct {
    PidConfig config;
    float integral, previous_error;
    uint32_t saturation_count;
    bool has_previous;
} PidController;
bool pid_init(PidController *pid, const PidConfig *config);
void pid_reset(PidController *pid);
float pid_update(PidController *pid, float target_rpm, float measured_rpm,
                 float dt_seconds, bool feedback_valid);

#endif
