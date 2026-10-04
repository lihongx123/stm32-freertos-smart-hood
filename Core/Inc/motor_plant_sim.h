#ifndef MOTOR_PLANT_SIM_H
#define MOTOR_PLANT_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "motor_control.h"

/* Deterministic software plant and Hall edge source; never a board driver. */
typedef struct {
    float rpm;
    float pulse_phase;
    float max_rpm;
    float time_constant_seconds;
    uint32_t clock_us;
    uint32_t emitted_edges;
} MotorPlantSim;

bool motor_plant_sim_init(MotorPlantSim *plant, float max_rpm,
                          float time_constant_seconds);
uint32_t motor_plant_sim_step(MotorPlantSim *plant, HallEstimator *hall,
                               float duty, uint32_t step_us, bool stalled,
                               bool hall_output_enabled);

#endif
