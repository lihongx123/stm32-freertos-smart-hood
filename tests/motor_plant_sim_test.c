#include "motor_plant_sim.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    MotorPlantSim plant;
    HallEstimator hall;
    uint32_t measured = 0;
    assert(!motor_plant_sim_init(&plant, 0.0f, 0.3f));
    assert(motor_plant_sim_init(&plant, 3000.0f, 0.3f));
    assert(hall_init(&hall, 2, 1000, 100000, 150000));
    for (unsigned i = 0; i < 200; ++i)
        (void)motor_plant_sim_step(&plant, &hall, 0.5f, 20000, false, true);
    assert(plant.rpm > 1490.0f && plant.rpm < 1510.0f);
    assert(plant.emitted_edges > 100);
    assert(hall_read_rpm(&hall, plant.clock_us, &measured));
    assert(measured > 1400 && measured < 1600);

    const uint32_t before_loss = plant.emitted_edges;
    for (unsigned i = 0; i < 10; ++i)
        assert(motor_plant_sim_step(&plant, &hall, 0.5f, 20000,
                                    false, false) == 0);
    assert(plant.emitted_edges == before_loss);
    assert(!hall_read_rpm(&hall, plant.clock_us, &measured));
    assert(hall_feedback_lost(&hall, plant.clock_us, 0, true));

    assert(hall_init(&hall, 2, 1000, 100000, 150000));
    for (unsigned i = 0; i < 10; ++i)
        (void)motor_plant_sim_step(&plant, &hall, 0.5f, 20000, false, true);
    assert(hall_read_rpm(&hall, plant.clock_us, &measured));
    for (unsigned i = 0; i < 100; ++i)
        (void)motor_plant_sim_step(&plant, &hall, 0.5f, 20000, true, true);
    assert(plant.rpm < 10.0f);
    assert(hall_feedback_lost(&hall, plant.clock_us, 0, true));
    puts("PASS software Hall pulses, RPM feedback, loss, recovery and stall");
    return 0;
}
