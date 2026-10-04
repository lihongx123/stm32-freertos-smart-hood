#include "motor_plant_sim.h"
#include <math.h>
#include <string.h>

bool motor_plant_sim_init(MotorPlantSim *plant, float max_rpm,
                          float time_constant_seconds)
{
    if (!plant || !isfinite(max_rpm) || !isfinite(time_constant_seconds) ||
        max_rpm <= 0.0f || time_constant_seconds <= 0.0f) return false;
    memset(plant, 0, sizeof(*plant));
    plant->max_rpm = max_rpm;
    plant->time_constant_seconds = time_constant_seconds;
    return true;
}

uint32_t motor_plant_sim_step(MotorPlantSim *plant, HallEstimator *hall,
                               float duty, uint32_t step_us, bool stalled,
                               bool hall_output_enabled)
{
    if (!plant || !hall || !hall->pulses_per_revolution || !step_us ||
        !isfinite(duty)) return 0;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    const float before = plant->rpm;
    const float dt = (float)step_us / 1000000.0f;
    const float equilibrium = stalled ? 0.0f : duty * plant->max_rpm;
    float gain = dt / plant->time_constant_seconds;
    if (gain > 1.0f) gain = 1.0f;
    plant->rpm += (equilibrium - plant->rpm) * gain;
    if (plant->rpm < 0.0f) plant->rpm = 0.0f;

    /* Integrate revolutions over this step, then place edge crossings within it. */
    const float pulses = (before + plant->rpm) * 0.5f *
        (float)hall->pulses_per_revolution * dt / 60.0f;
    const float phase_before = plant->pulse_phase;
    const float total = phase_before + pulses;
    const uint32_t crossings = (uint32_t)total;
    plant->pulse_phase = total - (float)crossings;
    uint32_t emitted = 0;
    if (hall_output_enabled && pulses > 0.0f) {
        for (uint32_t i = 0; i < crossings; ++i) {
            const float position = (1.0f - phase_before + (float)i) / pulses;
            const uint32_t offset = (uint32_t)(position * (float)step_us);
            (void)hall_on_edge(hall, plant->clock_us + offset);
            emitted++;
        }
    }
    plant->clock_us += step_us;
    plant->emitted_edges += emitted;
    return emitted;
}
