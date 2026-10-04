#include "motor_control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const MotorTargets targets = {600, 1200, 1800, 2400};
    assert(motor_target_rpm(FAN_OFF, &targets) == 0);
    assert(motor_target_rpm(FAN_HIGH, &targets) == 1800);
    assert(motor_target_rpm(FAN_BOOST, NULL) == 0);

    HallEstimator hall;
    uint32_t rpm = 0;
    assert(!hall_init(&hall, 0, 1000, 100000, 150000));
    assert(hall_init(&hall, 2, 1000, 100000, 150000));
    assert(!hall_on_edge(&hall, 0xfffffff0u));
    assert(hall_on_edge(&hall, 0x00002700u));
    assert(hall_read_rpm(&hall, 0x00002700u, &rpm));
    assert(rpm == 3000); /* unsigned wrap gives a 10,000 us period. */
    assert(!hall_feedback_lost(&hall, 0x00002700u, 0, true));
    assert(hall_feedback_lost(&hall, 0x00030000u, 0, true));
    assert(!hall_read_rpm(&hall, 0x00030000u, &rpm));
    assert(!hall_on_edge(&hall, 0x00030001u)); /* invalid long period */
    assert(!hall_read_rpm(&hall, 0x00030001u, &rpm));
    assert(hall_feedback_lost(&hall, 0x00030001u, 0, true));
    assert(!hall_feedback_lost(&hall, 0x00030001u, 0, false));

    const PidConfig config = {0.01f, 0.1f, 0.001f, -5.f, 5.f, 0.f, 1.f};
    PidController pid;
    assert(pid_init(&pid, &config));
    assert(pid_update(&pid, 0, 0, 0.02f, true) == 0);
    assert(pid_update(&pid, 100, 100, 0.02f, true) == 0);
    assert(pid_update(&pid, 100, 0, 0.02f, true) == 1);
    for (int i = 0; i < 100; i++)
        assert(pid_update(&pid, 100, 0, 0.02f, true) == 1);
    assert(pid.integral <= 2.01f); /* bounded after reaching output limit */
    assert(pid.saturation_count > 0);
    assert(pid_update(&pid, 100, 100, 0.02f, false) == 0);
    assert(pid.integral == 0 && !pid.has_previous);
    puts("PASS motor targets, Hall wrap/timeout, PID clamp/anti-windup/failure");
    return 0;
}
