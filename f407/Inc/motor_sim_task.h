#ifndef F407_MOTOR_SIM_TASK_H
#define F407_MOTOR_SIM_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"

enum { MOTOR_SIM_STALL = 1u, MOTOR_SIM_HALL_LOSS = 2u };

typedef struct {
    uint32_t target_rpm;
    uint32_t actual_rpm;
    uint32_t hall_rpm;
    uint32_t hall_edges;
    uint32_t duty_permyriad;
    uint32_t pid_saturation_count;
    uint32_t fault_mask, stall_events, feedback_loss_events;
    TickType_t heartbeat;
} MotorSimMetrics;

void MotorSimTask(void *argument);
MotorSimMetrics motor_sim_snapshot(void);
void motor_sim_set_fault(uint32_t mask, bool enabled);

#endif
